#include "pch.h"
#include "cache.h"
#include "local.h"
#include "pager.h"
#include "sighandle.h"
#include "state.h"
#include "status_line.h"
#include "tui.h"
#include "tui_input.h"
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

    tui_resized();

    g_tui->mode = TUI_MODE_NORMAL;
    g_tui->input_caret = 0;
    g_tui->input_len = 0;
    g_tui->input_prompt_len = 0;
    g_tui->cb_input_complete = NULL;
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
    for (char buf[16];; read_n = read(STDOUT_FILENO, &buf, sizeof(buf)))
    {
        if (read_n < 0 || *buf == '\0')
        {
            // We need this sleep or else the program pins a core to 100%
            // usage...
            usleep(1);
            continue;
        }

        for (int x = 0; x < read_n; ++x)
        {
            // TODO: properly handle the full read buffer
            if (tui_handle_input(buf[x]) < 0) return -1;
        }
    }

    return 0;
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

    pager_paint();
    status_line_paint();
}

void
tui_invalidate(void)
{
    int cursor_x_prev = g_tui->cursor_x,
        cursor_y_prev = g_tui->cursor_y;

    pager_paint();

    g_statline.components[STATUS_LINE_COMPONENT_RIGHT].invalidated
        = true;
    status_line_paint();

    // This stops cursor flying off when in link mode
    tui_cursor_move(cursor_x_prev, cursor_y_prev);
}

/* Goto site that is currently in the input buffer */
void
tui_go_from_input(void)
{
    // Parse the URI.
    struct uri uri = uri_parse(g_tui->input, g_tui->input_len);

    // We need to make sure that there is a protocol or else the URI parse
    // will cause problems
    if (uri.protocol == PROTOCOL_NONE)
    {
        // Re-parse it with a 'gemini://' prefix
        char tmp[g_tui->input_len + 1];
        strncpy(tmp, g_tui->input, g_tui->input_len);
        g_tui->input_len = snprintf(g_tui->input, TUI_INPUT_BUFFER_MAX,
            "gemini://%s", tmp);
        uri = uri_parse(g_tui->input, g_tui->input_len);
    }
    tui_go_to_uri(&uri, true, false);
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

    // Show status message
    tui_status_begin();
    tui_printf("Loading %s ...", g_tui->input);
    tui_status_end();

    int success;
    bool do_cache = false;

    // Handle protocol/requests
    switch (uri.protocol)
    {
    case PROTOCOL_GEMINI:
    case PROTOCOL_GOPHER:
        success = uri.protocol == PROTOCOL_GEMINI
            ? gemini_request(&uri)
            : gopher_request(&uri);

        if (success == 0)
        {
            tui_status_begin();
            tui_printf("Loaded content from %s, ", uri.hostname);
            tui_print_size(g_recv->size);
            tui_status_end();

            if (!force_nocache) do_cache = true;
        }
        break;

    case PROTOCOL_FILE:
        // Local file/directory; try to read it.
        int is_dir;
        success = local_request(&uri, &is_dir);

        if (success == 0)
        {
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
        // Update current URI state
        memcpy(&g_state->uri, uri_in, sizeof(struct uri));

        int sel, scroll;
        if (push_hist)
        {
            history_push(&g_state->uri,
                g_pager->selected_link_index, g_pager->scroll);
            sel = -1;
            scroll = 0;
        }
        else
        {
            // Page is presumably from history, so read the
            // last selection/scroll values
            sel = g_hist->ptr->last_sel;
            scroll = g_hist->ptr->last_scroll;
        }

        // Update the pager
        pager_update_page(sel, scroll);

        if (do_cache)
        {
            cache_push_current();
        }
    }
    return success;
}
