#include "pch.h"
#include "gopher.h"
#include "state.h"
#include "tui.h"
#include "uri.h"

void
gopher_deinit(void)
{
    struct gopher *ph = &g_state->ph;
    if (ph->sock) close(ph->sock);
}

int
gopher_request(struct uri *uri)
{
    struct gopher *const ph = &g_state->ph;

    if (!uri ||
        !uri->hostname ||
        uri->hostname[0] == '\0') return -1;

    ph->sock = connect_socket_to(
        uri->hostname,
        uri->port == 0 ? 70 : uri->port);
    if (!ph->sock)
    {
        goto fail;
    }

    tui_status_say("Successful connection");

    // Create the gopher request
    // <url><cr-lf> (same as Gemini)
    static char request[1024];
    size_t len = snprintf(request, sizeof(request),
        "%s\r\n", uri->path);
    if (write(ph->sock, request, len) == -1)
    {
        tui_status_begin();
        tui_printf("Error while sending data to %s", uri->hostname);
        tui_status_end();
        goto fail;
    }

    // Read response body in chunks
    size_t recv_bytes = 0;
    int response_code;
    for (char chunk[512];
        (response_code = read(ph->sock, chunk, sizeof(chunk))) > 0;)
    {
        recv_buffer_check_size(recv_bytes + response_code);
        memcpy(g_recv->b + recv_bytes, chunk, response_code);
        recv_bytes += response_code;
    }
    g_recv->size = 0;
    if (response_code < 0)
    {
        tui_status_begin();
        tui_printf("Error reading server response body");
        tui_status_end();
        goto fail;
    }
    g_recv->size = recv_bytes;

    // Update MIME; for now everything is plaintext
    mime_parse(&g_recv->mime, MIME_GOPHERMAP, strlen(MIME_GOPHERMAP) + 1);

fail:
    close(ph->sock);
    ph->sock = 0;

    return 0;
}
