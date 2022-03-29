#include "pch.h"
#include "gemini.h"
#include "state.h"
#include "tofu.h"
#include "tui.h"

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

    gem->sock = connect_socket_to(
        uri->hostname,
        uri->port == 0 ? 1965 : uri->port);
    if (!gem->sock)
    {
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

    tui_status_say("TLS handshake ...");

    // TLS handshake
    int ssl_status;
    if ((ssl_status = SSL_connect(gem->ssl)) != 1)
    {
    #if 0
        unsigned long reason = ERR_get_error();
        const char *reason_str = ERR_reason_error_string(reason);
    #endif

        tui_status_begin();
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
        tui_status_end();
        goto fail;
    }

    tui_status_say("Successful connection");

    // Verify certificate (using TOFU)
    X509 *cert = SSL_get_peer_certificate(gem->ssl);
    if (cert == NULL)
    {
        // Somehow no certificate was presented
        tui_status_say("error: server did not present a certificate");
        goto fail;
    }
    else
    {
        int tofu_status = tofu_verify_or_add(uri->hostname, cert);
        switch (tofu_status)
        {
        case TOFU_VERIFY_OK:
            tui_status_say("tofu: host fingerprints match");
            break;
        case TOFU_VERIFY_FAIL:
            // TODO: prompt user to decide whether to trust new certificate
            tui_status_say("tofu: fingerprint mismatch!");
            goto fail;
        case TOFU_VERIFY_NEW:
            tui_status_say("tofu: blindly trusting certificate "
                "from unrecognised host");
            break;
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
        tui_status_begin();
        tui_printf("Error while reading response header data (error %d)",
            ssl_error);
        tui_status_end();
        goto fail;
    }

    int response_header_len = strcspn(response_header, "\r");
    int response_header_len_mime = strcspn(response_header, "\r;");
    response_header[response_header_len_mime] = '\0';
    response_header[response_header_len] = '\0';

    tui_status_begin();
    tui_printf("Server responded: %s", response_header);
    tui_status_end();

    // Interpret response code
    switch(response_header[0])
    {
        // Success code
        case '2':
            char chunk[512];
            size_t recv_bytes = 0;

            // Copy MIME type
            mime_parse(&g_recv->mime,
                response_header + 2 + 1,
                response_header_len_mime - 2 - 1);

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
                tui_status_say("Error reading server response body");
                goto fail;
            }
            g_recv->size = recv_bytes;

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
            tui_status_begin();
            tui_printf("Redirecting to %s", redirect_uri_str);
            tui_status_end();

            tui_go_to_uri(&redirect_uri, true, false);

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
