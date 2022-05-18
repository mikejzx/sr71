#include "pch.h"
#include "gopher.h"
#include "state.h"
#include "tui.h"
#include "tui_input_prompt.h"
#include "uri.h"

static struct uri s_search_uri = { 0 };

static void gopher_search_complete(void);

void
gopher_deinit(void)
{
    struct gopher *ph = &g_state.ph;
    if (ph->sock) close(ph->sock);
}

int
gopher_request(struct uri *uri)
{
#if !PROTOCOL_SUPPORT_GOPHER
    return -1;
#else
    int ret_status = -1;
    struct gopher *const ph = &g_state.ph;

    if (!uri ||
        !uri->hostname ||
        uri->hostname[0] == '\0') return -1;

    // Gopher search: give an input prompt, and request link with input as
    //                the query.
    if (uri->gopher_item == GOPHER_ITEM_SEARCH &&
        !*uri->query)
    {
        s_search_uri = *uri;

        // Show the input prompt
        tui_input_prompt_begin(
            TUI_MODE_INPUT,
            "Enter gopher search query: ", 0,
            NULL,
            gopher_search_complete);

        // Need to return an error or else the TUI will think we successfully
        // requested something.  The gopher_search_complete will navigate to
        // new search page for us.
        return -1;
    }

    ph->sock = connect_socket_to(
        uri->hostname,
        uri->port == 0 ? 70 : uri->port);
    if (!ph->sock)
    {
        goto fail;
    }

    tui_status_say("Successful connection");

    // Create the gopher request
    static char request[1024];
    size_t len;
    if (uri->gopher_item != GOPHER_ITEM_SEARCH)
    {
        // <url><cr-lf> (same as Gemini)
        len = snprintf(request, sizeof(request),
            "%s\r\n", uri->path);
    }
    else
    {
        // Search selector
        // <url>\t<search query><cr-lf><tab>
        len = snprintf(request, sizeof(request),
            "%s\t%s\r\n", uri->path, uri->query);
    }
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

    ret_status = 0;

    // Get MIME type
    mime_parse(
        &g_recv->mime,
        gopher_item_to_mime(uri->gopher_item), MIME_TYPE_MAX);

fail:
    close(ph->sock);
    ph->sock = 0;

    return ret_status;

#endif // PROTOCOL_SUPPORT_GOPHER
}

/* Search prompt completion callback */
static void
gopher_search_complete(void)
{
    // Repeat request to the same URI, but with the input the query component
    struct uri uri = s_search_uri;
    uri_set_query(&uri, g_in->buffer);
    tui_go_to_uri(&uri, true, true);
}
