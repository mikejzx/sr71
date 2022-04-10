#include "pch.h"
#include "cache.h"
#include "local.h"
#include "pager.h"
#include "sighandle.h"
#include "state.h"
#include "status_line.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_input_prompt.h"
#include "uri.h"

struct tui_state *g_tui;
static struct termios termios_initial;
extern void program_exited(void);

void
tui_init(void)
{
    setlocale(LC_ALL, "C.UTF-8");

    g_tui = &g_state->tui;

    {
        struct termios t;
        tcgetattr(STDOUT_FILENO, &t);

        // Save initial terminal state for restoring later
        termios_initial = t;

        // Disable echoing and canonical mode
        t.c_lflag &= ~(ECHO | ICANON);

        tcsetattr(STDOUT_FILENO, TCSANOW, &t);

        fcntl(STDOUT_FILENO, F_SETFL, O_NONBLOCK);
        fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    }

    // Enter alternate buffer
    tui_say("\x1b[?1049h");

    // Register cleanup function
    atexit(program_exited);

    // Cleanup the alternative buffer
    tui_say("\x1b[2J");

    // Hide cursor
    tui_say("\x1b[?25l");
    tui_cursor_move(0, 0);

    tui_input_init();

    tui_resized();
}

void
tui_cleanup(void)
{
    // Cleanup alternate buffer
    tui_say("\x1b[2J");

    // Goto normal buffer
    tui_say("\x1b[?1049l");

    // Show cursor again
    tui_say("\x1b[?25h");

    // Restore initial state
    tcsetattr(STDOUT_FILENO, TCSANOW, &termios_initial);
}

int
tui_update(void)
{
    ssize_t read_n = 0;
    for (char buf[16];
        !g_tui->did_quit;
        read_n = read(STDOUT_FILENO, &buf, sizeof(buf)))
    {
        if (read_n < 0 || *buf == '\0')
        {
            // We need this sleep or else the program pins a core to 100%
            // usage...
            usleep(1);
            continue;
        }

        // Handle input
        if (tui_input_handle(buf, read_n) == TUI_QUIT) return -1;
    }

    return g_tui->did_quit ? -1 : 0;
}

void
tui_resized(void)
{
    // Get new window size
    struct winsize ws;
    ioctl(1, TIOCGWINSZ, &ws);
    g_tui->w = ws.ws_col;
    g_tui->h = ws.ws_row;

    pager_resized();

    // Repaint screen
    tui_repaint(true);
}

/* Repaint entire screen */
void
tui_repaint(bool clear)
{
    tui_cursor_move(0, 0);

    if (clear) tui_say("\x1b[2J");

    pager_paint(true);
    status_line_paint();

    tui_input_prompt_redraw_full();
}

void
tui_invalidate(enum tui_invalidate_flags flags)
{
    if (!flags) return;

    int cursor_x_prev = g_tui->cursor_x,
        cursor_y_prev = g_tui->cursor_y;

    if (flags & (INVALIDATE_PAGER_SELECTED_BIT | INVALIDATE_PAGER_BIT))
    {
        pager_paint(flags & INVALIDATE_PAGER_BIT);
    }

    if (flags & INVALIDATE_STATUS_LINE_BIT)
    {
        g_statline.components[STATUS_LINE_COMPONENT_RIGHT].invalidated
            = true;
        status_line_paint();
    }

    // This stops cursor flying off all over the place when in link mode
    tui_cursor_move(cursor_x_prev, cursor_y_prev);
}

void
tui_quit(void)
{
    g_tui->did_quit = true;
}

/* Goto site that is currently in the input buffer */
void
tui_go_from_input(void)
{
    // Parse the URI.
    struct uri uri = uri_parse(g_in->buffer, g_in->buffer_len + 1);

    // We need to make sure that there is a protocol or else the URI parse
    // will cause problems
    if (uri.protocol == PROTOCOL_NONE)
    {
        // Re-parse it with a 'gemini://' prefix
        char tmp[g_in->buffer_len + 1];
        strncpy(tmp, g_in->buffer, g_in->buffer_len + 1);
        g_in->buffer_len = snprintf(g_in->buffer, TUI_INPUT_BUFFER_MAX,
            "gemini://%s", tmp) - 1;
        uri = uri_parse(g_in->buffer, g_in->buffer_len + 1);
    }
    tui_go_to_uri(&uri, true, false);
}

static inline int
tui_get_register_index(const char c)
{
    if      (c <  '0') return 0;
    else if (c <= '9') return c - '0';
    else if (c <= 'Z') return c - 'A' + ('9' - '0' + 1);
    else if (c <= 'z') return c - 'a' + ('Z' - 'A' + 1) + ('9' - '0' + 1);
    else               return 0;
}

/* Set a mark at current pos to the register input buffer */
void
tui_set_mark_from_input(void)
{
    if (!g_in->buffer_len) return;
    g_pager->marks[tui_get_register_index(*g_in->buffer)] = g_pager->scroll;
}

/* Follow mark that is currently in input buffer */
void
tui_goto_mark_from_input(void)
{
    if (!g_in->buffer_len) return;
    g_pager->scroll = g_pager->marks[tui_get_register_index(*g_in->buffer)];
    tui_invalidate(INVALIDATE_PAGER_BIT | INVALIDATE_STATUS_LINE_BIT);
}

/* Select the next link on the page */
void
tui_select_next_link(void)
{
    if (!g_pager->link_count) return;

    g_pager->selected_link_index =
        (g_pager->selected_link_index + 1) % g_pager->link_count;
}

/* Select the previous link on the page */
void
tui_select_prev_link(void)
{
    if (!g_pager->link_count) return;

    --g_pager->selected_link_index;
    if (g_pager->selected_link_index < 0)
        g_pager->selected_link_index = g_pager->link_count - 1;
}

/* Follow selected link */
void
tui_follow_selected_link(void)
{
    if (g_pager->selected_link_index < 0 ||
        g_pager->selected_link_index >= g_pager->link_count) return;

    tui_go_to_uri(
        &g_pager->links[g_pager->selected_link_index].uri,
        true,
        false);
}

void
tui_update_link_peek(void)
{
    tui_status_begin_soft();

    int cursor_old = g_tui->cursor_x;

    // Clear everything after digits first
    tui_cursor_move(g_in->prompt_len + 1 + g_in->buffer_len, g_tui->h);
    tui_printf("%*s", g_tui->w - cursor_old, "");
    tui_cursor_move(cursor_old, g_tui->h);

    if (g_pager->selected_link_index < 0 ||
        g_pager->selected_link_index >= g_pager->link_count)
    {
        // Don't print anything else
        goto end;
    }

    // Convert selected URI to a string
    char uri_name[URI_STRING_MAX];
    uri_str(
        &g_pager->links[g_pager->selected_link_index].uri,
        uri_name,
        g_tui->w,
        URI_FLAGS_NONE);

    // Print URI of the selected link after digits
    tui_cursor_move(g_in->prompt_len + 1 + g_in->buffer_len, g_tui->h);
    tui_printf(" (%s)", uri_name);

    tui_cursor_move(cursor_old, g_tui->h);

end:
    tui_status_end();
    tui_invalidate(INVALIDATE_PAGER_SELECTED_BIT);
}

void
tui_search_refresh(void)
{
    struct search *const s = &g_pager->search;

    if (s->query_len <= 0) return;

    s->match_count = 0;
    s->invalidated = false;

    struct search_match match;

    // Search for all matches in the buffer itself
    for (int i = 0; i < g_pager->buffer.line_count; ++i)
    {
        struct pager_buffer_line *line = &g_pager->buffer.lines[i];

        // Simple linear search for the string
        for (const char *c = line->s;
            c < line->s + line->bytes;
            ++c)
        {
            //if (line->bytes - (c - line->s) < s->query_len ||
            //    strncmp(c, s->query, s->query_len) != 0) continue;

            // Check for match using our special search function
            if (pager_multiline_search(
                c, s->query, &g_pager->buffer, i, &match) != 0) continue;

            s->matches[s->match_count++] = (struct search_match)
            {
                .begin =
                {
                    .line = i,
                    .loc = c,
                },
                .end = match.end
            };
        }
    }

    s->index = -1;

    if (s->match_count == 0)
    {
        tui_status_say("Pattern not found");
        return;
    }
}

void
tui_search_refresh_forward(void)
{
    struct search *const s = &g_pager->search;
    s->reverse = false;

    s->query_len = g_in->buffer_len;
    if (!s->query_len) return;
    strncpy(s->query, g_in->buffer, sizeof(s->query));
    s->query[s->query_len] = '\0';

    tui_search_refresh();
    tui_search_next();
}

void
tui_search_refresh_reverse(void)
{
    struct search *const s = &g_pager->search;
    s->reverse = true;

    s->query_len = g_in->buffer_len;
    if (!s->query_len) return;
    strncpy(s->query, g_in->buffer, sizeof(s->query));
    s->query[s->query_len] = '\0';

    tui_search_refresh();
    s->index = max(s->match_count - 1, 0);
    tui_search_prev();
}

void
tui_search_next(void)
{
    if (g_pager->search.reverse) pager_search_prev();
    else pager_search_next();
}

void
tui_search_prev(void)
{
    if (g_pager->search.reverse) pager_search_next();
    else pager_search_prev();
}

void
tui_save_to_file(void)
{
    FILE *fp = fopen(g_in->buffer, "w");
    if (!fp)
    {
        tui_status_begin();
        tui_printf("Failed to open file '%s'", g_in->buffer);
        tui_status_end();
        return;
    }

    const char *buffer = g_recv->b_alt ? g_recv->b_alt : g_recv->b;
    size_t buffer_size = g_recv->size;

    fwrite(buffer, buffer_size, 1, fp);

    fclose(fp);

    tui_status_begin();
    tui_say("Wrote ");
    tui_print_size(buffer_size);
    tui_printf(" to '%s'", g_in->buffer);
    tui_status_end();
}

void
tui_refresh_page(void)
{
#if CACHE_USE_DISK
    // Store old checksum of buffer if this is a cached page
    unsigned char old_hash[EVP_MAX_MD_SIZE];
    unsigned old_hash_len = 0;

    if (g_pager->cached_page &&
        uri_cmp_notrailing(&g_pager->cached_page->uri, &g_state->uri) == 0)
    {
        old_hash_len = g_pager->cached_page->hash_len;
        memcpy(old_hash, g_pager->cached_page->hash, old_hash_len);
    }
#endif

    int status = tui_go_to_uri(&g_state->uri, false, true);

#if CACHE_USE_DISK
    if (status == 0 &&
        old_hash_len)
    {
        // Check if hashes match
        if (old_hash_len == g_pager->cached_page->hash_len &&
            memcmp(old_hash, g_pager->cached_page->hash, old_hash_len) == 0)
        {
            tui_status_say("content unchanged since last cache");
        }
        else
        {
            tui_status_say("new content since last cache");
        }
    }
#else
    (void)status;
#endif
}

/* Goto a site */
int
tui_go_to_uri(
    const struct uri *const uri_in,
    bool push_hist,
    bool force_nocache)
{
    static struct uri uri;
    memcpy(&uri, uri_in, sizeof(struct uri));

    if (uri.protocol == PROTOCOL_UNKNOWN ||
        uri.protocol == PROTOCOL_FINGER)
    {
        // Show error message
        tui_status_say("Unsupported protocol");
        return -1;
    }

    // Assume Gemini if no scheme given
    if (uri.protocol == PROTOCOL_NONE)
    {
        uri.protocol = PROTOCOL_GEMINI;
    }

    // All protocols except file need a hostname
    if (uri.protocol != PROTOCOL_FILE &&
        uri.hostname[0] == '\0')
    {
        tui_status_say("Invalid URI");
        return -1;
    }

    int success;
    bool do_cache = false;
    struct cached_item *cache_item = NULL;

    // For now we don't cache pages that have a URI query
    if (*uri.query) force_nocache = true;

    // Handle protocol/requests
    switch (uri.protocol)
    {
    case PROTOCOL_GEMINI:
    case PROTOCOL_GOPHER:
        bool from_cache = !force_nocache && cache_find(uri_in, &cache_item);
        if (!from_cache)
        {
            success = uri.protocol == PROTOCOL_GEMINI
                ? gemini_request(&uri)
                : gopher_request(&uri);
        }
        else
        {
            success = 0;
        }

        if (success == 0)
        {
            tui_input_prompt_end(g_in->mode);

            tui_status_begin();
            tui_printf("Loaded content from %s, ", uri.hostname);
            tui_print_size(g_recv->size);
            tui_status_end();

            do_cache = !from_cache;
        }
        break;

    case PROTOCOL_FILE: ;
        // Local file/directory; try to read it.
        int is_dir;
        success = local_request(uri_in, &is_dir);

        if (success == 0)
        {
            tui_input_prompt_end(g_in->mode);

            tui_status_begin();
            if (is_dir)
            {
                tui_printf("Loaded directory, %d entries", is_dir - 1);
            }
            else
            {
                tui_printf("Loaded local file, ");
                tui_print_size(g_recv->size);
            }
            tui_status_end();
        }
        break;

    default:
        success = -1;
        break;
    }

    if (success == 0)
    {
        tui_input_prompt_end(g_in->mode);

        // Update the last selection/scroll of last cached page
        if (g_pager->cached_page)
        {
            g_pager->cached_page->session.last_sel =
                g_pager->selected_link_index;
            g_pager->cached_page->session.last_scroll =
                g_pager->scroll;
        }

        // Update current URI state
        memcpy(&g_state->uri, uri_in, sizeof(struct uri));

        // Push the page to the cache
        if (do_cache) { g_pager->cached_page = cache_push_current(); }
        else g_pager->cached_page = NULL;

        int sel, scroll;

        // And get the new selection/scroll for the newly-loaded page
        if (cache_item)
        {
            sel = cache_item->session.last_sel;
            scroll = cache_item->session.last_scroll;
            g_pager->cached_page = cache_item;

            // Update gopher item type from cache
            if (uri.protocol == PROTOCOL_GOPHER)
            {
                g_state->uri.gopher_item =
                    gopher_mime_to_item(&cache_item->mime);
            }
        }
        else
        {
            sel = -1;
            scroll = 0;
            g_recv->b_alt = NULL;
        }

        // Push to undo/redo history
        if (push_hist)
        {
            history_push(&g_state->uri);
        }

        // Update the pager
        pager_update_page(sel, scroll);
    }
    return success;
}
