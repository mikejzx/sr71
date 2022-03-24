#include "pch.h"
#include "state.h"
#include "gemini.h"
#include "tui.h"
#include "pager.h"

void
gemini_init(void)
{
    struct gemini *const gem = &g_state->gem;

    // Initialise TLS.  We require at least TLS 1.2
    gem->ctx = SSL_CTX_new(TLS_method());
    SSL_CTX_set_min_proto_version(gem->ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(gem->ctx, SSL_VERIFY_NONE, NULL);

    // Cipher list from AV-98's list of "sensible ciphers"
    SSL_CTX_set_cipher_list(gem->ctx,
        "AESGCM+ECDHE:"
        "AESGCM+DHE:"
        "CHACHA20+ECDHE:"
        "CHACHA20+DHE:"
        "!DSS:"
        "!SHA1:"
        "!MD5:"
        "@STRENGTH");
}

void
gemini_deinit(void)
{
    struct gemini *const gem = &g_state->gem;

    if (gem->sock) close(gem->sock);

    SSL_CTX_free(gem->ctx);
}

int
gemini_request(struct uri *uri)
{
    int ret_status = -1;
    struct gemini *const gem = &g_state->gem;

    if (!uri ||
        !uri->hostname ||
        uri->hostname[0] == '\0') return -1;

    tui_cmd_status_prepare();
    tui_say("Looking up address ...");

    // I'll never know why on Earth getaddrinfo takes a string here
    char port_str[5];
    if (uri->port == 0)
    {
        strncpy(port_str, "1965", sizeof(port_str));
    }
    else
    {
        snprintf(port_str, sizeof(port_str), "%04d", uri->port);
    }

    // Get host addresses
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = IPPROTO_TCP;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    struct addrinfo *res = NULL;
    getaddrinfo(uri->hostname, port_str, &hints, &res);

    // Connect to any of the addresses we can
    bool connected = false;
    for (struct addrinfo *i = res; i != NULL; i = i->ai_next)
    {
        // Create socket
        if ((gem->sock = socket(
            i->ai_addr->sa_family,
            SOCK_STREAM, 0)) < 0)
        {
            //fprintf(stderr, "gemini: failed to create socket\n");
            tui_cmd_status_prepare();
            tui_say("error: failed to create socket");
            gem->sock = 0;
            continue;
        }

        // Setup timeout on socket
        static const struct timeval timeout =
        {
            .tv_sec = 5,
            .tv_usec = 0,
        };
        if (setsockopt(gem->sock, SOL_SOCKET, SO_RCVTIMEO,
            (char *)&timeout, sizeof(timeout)) < 0 ||
            setsockopt(gem->sock, SOL_SOCKET, SO_SNDTIMEO,
            (char *)&timeout, sizeof(timeout)) < 0)
        {
            tui_cmd_status_prepare();
            tui_say("error: failed to set socket timeout");
            gem->sock = 0;
            continue;
        }

        tui_cmd_status_prepare();
        tui_say("Connecting ...");

        // Connect socket
        if (connect(gem->sock, (struct sockaddr *)i->ai_addr,
            i->ai_addrlen) < 0)
        {
            //fprintf(stderr, "gemini: failed to connect to '%s'\n", uri->hostname);

            tui_cmd_status_prepare();
            tui_printf("error: failed to connect to %s", uri->hostname);
            gem->sock = 0;
            continue;
        }
        connected = true;

        break;
    }
    freeaddrinfo(res);
    if (!connected)
    {
        //fprintf(stderr, "gemini: no address for '%s'\n", uri->hostname);
        tui_cmd_status_prepare();
        if (res == NULL)
        {
            tui_printf("error: no addresses for '%s'", uri->hostname);
        }
        else
        {
            tui_printf("error: could not connect to '%s'", uri->hostname);
        }
        goto fail;
    }

    // Setup TLS
    gem->ssl = SSL_new(gem->ctx);
    SSL_set_fd(gem->ssl, gem->sock);
    SSL_set_connect_state(gem->ssl);
    SSL_set_verify(gem->ssl, SSL_VERIFY_NONE, NULL);
    SSL_ctrl(gem->ssl,
        SSL_CTRL_SET_TLSEXT_HOSTNAME,
        TLSEXT_NAMETYPE_host_name,
        (void *)uri->hostname);

    tui_cmd_status_prepare();
    tui_say("TLS handshake ...");

    // TLS handshake
    int ssl_status;
    if ((ssl_status = SSL_connect(gem->ssl)) != 1)
    {
        //fprintf(stderr, "gemini: TLS handshake failed '%s'\n", uri->hostname);

    #if 0
        unsigned long reason = ERR_get_error();
        const char *reason_str = ERR_reason_error_string(reason);
    #endif

        tui_cmd_status_prepare();
        if (ssl_status == 0)
        {
            tui_printf("error: TLS connection closed");
                //"(%lu: %s)", reason, reason_str);
        }
        else
        {
            tui_printf("error: failed to perform TLS handshake with %s",
                uri->hostname);
                //"(%lu: %s)", reason, reason_str);
        }
        goto fail;
    }

    tui_cmd_status_prepare();
    tui_say("Successful connection");

    // Verify certificate (using TOFU)
    X509 *cert = SSL_get_peer_certificate(gem->ssl);
    if (cert == NULL)
    {
        // Somehow no certificate was presented
        tui_cmd_status_prepare();
        tui_say("error: server did not present a certificate");
        goto fail;
    }
    else
    {
        // Get X509 fingerprint/digest
        const EVP_MD *digest = EVP_get_digestbyname("sha256");
        unsigned char fingerprint[EVP_MAX_MD_SIZE];
        unsigned fingerprint_len;
        X509_digest(cert, digest, fingerprint, &fingerprint_len);

        tui_cmd_status_prepare();
        tui_printf("cert fingerprint: ");
        for (int b = 0; b < fingerprint_len; ++b)
        {
            tui_printf("%02x%s",
                (unsigned char)fingerprint[b],
                b == fingerprint_len - 1 ? "" : ":");
        }

        X509_free(cert);
    }

    // Send the Gemini request!
    // <url><cr-lf>
    static char request[1024 + 2 + 1];
    size_t len = uri_str(uri, request, sizeof(request) - 2, 0);
    strcat(request, "\r\n");
    len += 2;
    SSL_write(gem->ssl, request, len);

    // Get response header
    // <status><space><meta><cr-lf>
    static char response_header[2 + 1 + 1024 + 2 + 1];
    int read_code, ssl_error;
    if ((read_code = SSL_read(gem->ssl, response_header,
            sizeof(response_header))) <= 0 &&
        (ssl_error = SSL_get_error(gem->ssl, read_code)) != SSL_ERROR_NONE)
    {
        tui_cmd_status_prepare();
        tui_printf("Error while reading response header data (error %d)",
            ssl_error);
        goto fail;
    }

    int response_header_len = strcspn(response_header, "\r");
    response_header[response_header_len] = '\0';
    tui_cmd_status_prepare();
    tui_printf("Server responded: %s", response_header);

    // Interpret response code
    switch(response_header[0])
    {
        // Success code
        case '2':
            char chunk[512];
            size_t recv_bytes = 0;

            // Read whatever was left from the header chunk after the header
            // (or else we end up with missing content...)
            response_header_len += 2; // mind the CR-LF
            if (read_code > response_header_len)
            {
                recv_buffer_check_size(
                    recv_bytes + read_code - response_header_len);
                memcpy(g_recv->b + recv_bytes,
                    response_header + response_header_len,
                    read_code - response_header_len);
                recv_bytes += read_code - response_header_len;
            }

            // Read response body in chunks
            int response_code;
            while ((response_code =
                SSL_read(gem->ssl, chunk, sizeof(chunk))) > 0)
            {
                recv_buffer_check_size(recv_bytes + response_code);
                memcpy(g_recv->b + recv_bytes, chunk, response_code);
                recv_bytes += response_code;
            }
            g_recv->size = 0;
            if (response_code < 0)
            {
                tui_cmd_status_prepare();
                tui_printf("Error reading server response body");
                goto fail;
            }
            g_recv->size = recv_bytes;

            // TODO: move this stuff away from this file
            tui_cmd_status_prepare();
            if (recv_bytes < 1024)
            {
                tui_printf("Loaded page from %s, %d b",
                    uri->hostname, (int)recv_bytes);
            }
            else if (recv_bytes < 1024 * 1024)
            {
                tui_printf("Loaded page from %s, %.2f KiB",
                    uri->hostname, recv_bytes / 1024.0f);
            }
            else
            {
                tui_printf("Loaded page from %s, %.2f MiB",
                    uri->hostname, recv_bytes / (1024.0f * 1024.0f));
            }

            ret_status = 0;
            break;

        // Redirect code
        case '3':
            // Get URI (position in response header is fixed)
            const char *redirect_uri_str = response_header + strlen("XX ");
            struct uri redirect_uri = uri_parse(
                redirect_uri_str,
                response_header_len - strlen("XX "));

            // Check the protocol and warn if it's cross
            if (g_state->uri.protocol != redirect_uri.protocol)
            {
                // TODO: warn about cross-protocol redirects
            }

            // TODO: don't follow more than N (5) consecutive redirects

            // Resolve URI in case it is relative
            uri_abs(&g_state->uri, &redirect_uri);

            // Perform redirect
            tui_cmd_status_prepare();
            tui_printf("Redirecting to %s", redirect_uri_str);
            tui_go_to_uri(&redirect_uri, true);

            break;

        // If status does not belong to 'SUCCESS' range of codes then socket
        // should be closed immediately
        default: goto fail;
    }

fail:
    if (gem->ssl)
    {
        SSL_free(gem->ssl);
        gem->ssl = NULL;
    }
    close(gem->sock);
    gem->sock = 0;

    return ret_status;
}
