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
    gem->ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(gem->ctx, TLS1_2_VERSION);
}

void
gemini_deinit(void)
{
    struct gemini *const gem = &g_state->gem;

    if (gem->sock) close(gem->sock);

    SSL_free(gem->ssl);
    SSL_CTX_free(gem->ctx);
}

int
gemini_request(struct uri *uri)
{
    int ret_status = -1;
    struct gemini *const gem = &g_state->gem;

    // Create socket
    if ((gem->sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        //fprintf(stderr, "gemini: failed to create socket\n");
        tui_cmd_status_prepare();
        tui_say("error: failed to create socket");
        gem->sock = 0;
        return -1;
    }

    // Setup timeout on socket
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(gem->sock, SOL_SOCKET, SO_RCVTIMEO,
        (char *)&timeout, sizeof(timeout)) < 0 ||
        setsockopt(gem->sock, SOL_SOCKET, SO_SNDTIMEO,
        (char *)&timeout, sizeof(timeout)) < 0)
    {
        tui_cmd_status_prepare();
        tui_say("error: failed to set socket timeout");
        goto close_socket;
    }

    // Setup TLS
    gem->ssl = SSL_new(gem->ctx);
    SSL_set_fd(gem->ssl, gem->sock);

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

    // Get host address
    struct addrinfo *server_addr = NULL, *res;
    getaddrinfo(uri->hostname, port_str, 0, &res);
    for (struct addrinfo *i = res; i != NULL; i = i->ai_next)
    {
        // TODO support for IPv6-only servers?
        //static char addr_str[INET6_ADDRSTRLEN];
        if (i->ai_addr->sa_family == AF_INET)
        {
            //struct sockaddr_in *p = (struct sockaddr_in *)i->ai_addr;
            server_addr = i;
            break;
        }
    }
    if (!server_addr)
    {
        //fprintf(stderr, "gemini: no address for '%s'\n", uri->hostname);
        tui_cmd_status_prepare();
        tui_printf("error: no address for to '%s'", uri->hostname);
        goto close_socket;
    }

    tui_cmd_status_prepare();
    tui_say("Connecting ...");

    // Connect socket
    if (connect(gem->sock, (struct sockaddr *)server_addr->ai_addr,
        server_addr->ai_addrlen) < 0)
    {
        //fprintf(stderr, "gemini: failed to connect to '%s'\n", uri->hostname);

        tui_cmd_status_prepare();
        tui_printf("error: failed to connect to %s", uri->hostname);
        goto close_socket;
    }

    tui_cmd_status_prepare();
    tui_say("TLS handshake ...");

    // TLS handshake
    if (SSL_connect(gem->ssl) != 1)
    {
        //fprintf(stderr, "gemini: TLS handshake failed '%s'\n", uri->hostname);

        tui_cmd_status_prepare();
        tui_printf("error: failed to perform TLS handshake with %s",
            uri->hostname);
        goto close_socket;
    }

    tui_cmd_status_prepare();
    tui_say("Successful connection");

    // Verify certificate (using TOFU)
    X509 *cert = SSL_get_peer_certificate(gem->ssl);
    if (cert == NULL)
    {
        // Somehow no certificate was presented
        tui_cmd_status_prepare();
        tui_say("warning: server did not present a certificate");
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
        goto close_socket;
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
                goto close_socket;
            }
            g_recv->size = recv_bytes;

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
        default: goto close_socket;
    }

close_socket:
    // Close socket
    close(gem->sock);
    gem->sock = 0;

    return ret_status;
}
