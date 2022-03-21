#include "pch.h"
#include "state.h"
#include "tui.h"
#include "sighandle.h"
#include "pager.h"
#include "status_line.h"
#include "uri.h"

struct tui_state *g_tui;
static struct termios termios_initial;
extern void program_exited(void);
static void tui_go_from_input(void);
static void tui_clear_cmd(void);

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
    bool invalidate = false;
    enum tui_mode_id next_mode = TUI_MODE_UNKNOWN;

    ssize_t read_n = 0;
    for (char buf = 0;; read_n = read(STDOUT_FILENO, &buf, 1))
    {
        if (read_n < 0 || buf == '\0')
        {
            // We need this sleep or else the program pins a core to 100%
            // usage...
            usleep(1);
            continue;
        }

        if (g_tui->mode == TUI_MODE_NORMAL)
        {
            // Don't parse escapes or new-lines
            if (buf == '\n' || buf == '\x1b') continue;

            // Handle input
            switch (buf)
            {
            // Vertical motion
            case 'j':
            case ('E' ^ 0100):
                pager_scroll(1);
                invalidate = true;
                break;
            case 'k':
            case ('Y' ^ 0100):
                pager_scroll(-1);
                invalidate = true;
                break;
            case 'd':
            case ('D' ^ 0100):
                pager_scroll(g_tui->h / 4);
                invalidate = true;
                break;
            case 'u':
            case ('U' ^ 0100):
                pager_scroll(g_tui->h / -4);
                invalidate = true;
                break;
            case 'f':
            case ('F' ^ 0100):
                pager_scroll(g_tui->h);
                invalidate = true;
                break;
            case 'b':
            case ('B' ^ 0100):
                pager_scroll(-g_tui->h);
                invalidate = true;
                break;
            case 'g':
                pager_scroll_top();
                invalidate = true;
                break;
            case 'G':
                pager_scroll_bottom();
                invalidate = true;
                break;

            // 'o': open page.
            case 'o':
            {
                // Prompt user for URL to visit
                next_mode = TUI_MODE_INPUT;

                const char *PROMPT = "go: ";
                g_tui->input_prompt_len = strlen(PROMPT);
                strncpy(g_tui->input_prompt, PROMPT, g_tui->input_prompt_len);

                const char *INPUT_DEFAULT = "gemini://";
                g_tui->input_caret = strlen(INPUT_DEFAULT);
                strncpy(g_tui->input, INPUT_DEFAULT, g_tui->input_caret + 1);

                g_tui->cb_input_complete = tui_go_from_input;
            } break;

            // 'e' to edit current page link in a 'go' command
            case 'e':
            {
                struct uri *uri = &g_state->gem.uri;
                char uri_string[256];
                size_t uri_strlen =
                    uri_str(uri, uri_string, sizeof(uri_string), 0);

                next_mode = TUI_MODE_INPUT;

                const char *PROMPT = "go: ";
                g_tui->input_prompt_len = strlen(PROMPT);
                strncpy(g_tui->input_prompt, PROMPT, g_tui->input_prompt_len);

                g_tui->input_caret = g_tui->input_len = uri_strlen;
                strncpy(g_tui->input, uri_string, g_tui->input_len + 1);

                g_tui->cb_input_complete = tui_go_from_input;
            } break;

            // Pressing a number goes into links mode
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            // 'J' and 'K' also put us in this mode
            case 'J':
            case 'K':
            case 'n':
            case 'p':
            {
                // Don't switch if there's no bloody links
                if (!g_pager->link_count) break;

                next_mode = TUI_MODE_LINKS;

                // Format of the prompt is:
                // 'follow link X ? (scheme://the.url/that/link/corresponds/to)
                const char *PROMPT = "follow link ";
                g_tui->input_prompt_len = strlen(PROMPT);
                strncpy(g_tui->input_prompt, PROMPT, g_tui->input_prompt_len);

                // We reset input here; though since on mode change we goto
                // links_mode_begin label, it will automatically get added
                g_tui->input_caret = 0;
                g_tui->input_len = 0;

                // This is for 'J' and 'K' and simply sets the selected link to
                // be the first one *on the visible screen*, rather than the
                // first in the entire buffer.  We have to offset it by one (as
                // the 'J' and 'K' handlers in the link mode will add/subtract
                // one link respectively).  Bit hacky but it works
                if (buf == 'J' || buf == 'n')
                {
                    pager_select_first_link_visible();
                    --g_pager->selected_link_index;
                }
                else if (buf == 'K' || buf == 'p')
                {
                    pager_select_last_link_visible();
                    ++g_pager->selected_link_index;
                }
            } break;

            // For now 'q' to exit
            case 'q':
                exit(0);
                break;

            default: break;
            }
        }
        else if (g_tui->mode == TUI_MODE_COMMAND)
        {
            // Not implemented
            exit(0);
        }
        else if (g_tui->mode == TUI_MODE_INPUT)
        {
            // TODO: proper scrolling or something
            int x_pos = min(
                1 + g_tui->input_prompt_len +
                (int)g_tui->input_caret, g_tui->w);
            tui_cursor_move(x_pos, g_tui->h);

            if (buf == (0100 ^ 'W'))
            {
                /* Ctrl-W: backspace a whole word (TODO) */
                goto backspace;
            }
            if (buf == '\n')
            {
                /* Return key; end of input */

                // Execute callback
                if (g_tui->cb_input_complete)
                {
                    g_tui->cb_input_complete();
                }

                // Change back to normal input
                next_mode = TUI_MODE_NORMAL;
                goto exit_block;
            }
            if (buf == 0x7f)
            {
            backspace:
                /* Backspace key; move caret backward */
                if (g_tui->input_caret <= 0)
                {
                    // Can't backspace any further
                    continue;
                }
                g_tui->input[g_tui->input_caret - 1] = '\0';
                tui_cursor_move(x_pos - 1, g_tui->h);
                tui_say(" ");
                tui_cursor_move(x_pos - 1, g_tui->h);
                --g_tui->input_caret;

                continue;
            }
            if (buf == '\x1b')
            {
                /* Escape key; cancel input */
                next_mode = TUI_MODE_NORMAL;
                goto exit_block;
            }
            // TODO: arrow keys

            // Check that we don't overflow the input
            if (g_tui->input_caret + 1 >= TUI_INPUT_BUFFER_MAX)
            {
                continue;
            }

            g_tui->input[g_tui->input_caret] = buf;

            // Add the text to the buffer
            g_tui->input[g_tui->input_caret + 1] = '\0';

            ++g_tui->input_caret;

            tui_sayn(&buf, 1);

            tui_cursor_move(x_pos + 1, g_tui->h);
        }
        else if (g_tui->mode == TUI_MODE_PASSWORD)
        {
            // Not implemented
            exit(0);
        }
        else if (g_tui->mode == TUI_MODE_LINKS)
        {
        links_mode_begin:
            // Relocate to caret pos
            int x_pos = min(
                1 + g_tui->input_prompt_len +
                (int)g_tui->input_caret, g_tui->w);
            tui_cursor_move(x_pos, g_tui->h);

            switch (buf)
            {
            // We allow some movement controls in link mode
            case 'j':
                pager_scroll(1);
                invalidate = true;
                goto exit_block;
            case 'k':
                pager_scroll(-1);
                invalidate = true;
                goto exit_block;
            case 'd':
            case ('D' ^ 0100):
                pager_scroll(g_tui->h / 2);
                invalidate = true;
                goto exit_block;
            case 'u':
            case ('U' ^ 0100):
                pager_scroll(g_tui->h / -2);
                invalidate = true;
                goto exit_block;
            case ('F' ^ 0100):
                pager_scroll(g_tui->h);
                invalidate = true;
                goto exit_block;
            case 'b':
            case ('B' ^ 0100):
                pager_scroll(-g_tui->h);
                invalidate = true;
                goto exit_block;
            case 'g':
                pager_scroll_top();
                invalidate = true;
                goto exit_block;
            case 'G':
                pager_scroll_bottom();
                invalidate = true;
                goto exit_block;

            /* Ctrl-W: clear whole thing */
            case (0100 ^ 'W'):
                g_tui->input_caret = 0;
                g_tui->input_len = 0;
                g_tui->input[0] = '\0';
                x_pos = min(g_tui->input_prompt_len, g_tui->w);
                goto update_link_peek;

            // Return or 'f': follow link
            case '\n':
            case 'f':
                // Find the URI that link corresponds to
                if (g_pager->selected_link_index >= 0 &&
                    g_pager->selected_link_index < g_pager->link_count)
                {
                    // Follow link
                    struct uri *uri =
                        &g_pager->links[g_pager->selected_link_index].uri;
                    tui_go_to_uri(uri);
                }
                else
                {
                    tui_cmd_status_prepare();
                    tui_say("No such link");
                }

                // Change back to normal input
                next_mode = TUI_MODE_NORMAL;
                goto exit_block;

            /* backspace key; delete and move caret backward */
            case 0x7f:
                if (g_tui->input_caret <= 0)
                {
                    // Can't backspace any further
                    continue;
                }
                g_tui->input[g_tui->input_caret - 1] = '\0';
                tui_cursor_move(x_pos - 1, g_tui->h);
                tui_say(" ");
                tui_cursor_move(x_pos - 1, g_tui->h);
                --g_tui->input_caret;
                --g_tui->input_len;

                x_pos -= 2;
                goto update_link_peek;

            /* 'x' delete digit under cursor */
            case 'x':
                goto update_link_peek;

            /* Escape key (or 'q'): cancel input */
            case '\x1b':
            case 'q':
                tui_clear_cmd();
                g_pager->selected_link_index = -1;
                invalidate = true;
                next_mode = TUI_MODE_NORMAL;
                goto exit_block;

            /* 'e' to use/edit the link in a 'go' command */
            case 'e':
            {
                if (g_pager->selected_link_index < 0 ||
                    g_pager->selected_link_index >= g_pager->link_count)
                {
                    continue;
                }
                struct uri *uri =
                    &g_pager->links[g_pager->selected_link_index].uri;
                char uri_string[256];
                size_t uri_strlen =
                    uri_str(uri, uri_string, sizeof(uri_string), 0);

                next_mode = TUI_MODE_INPUT;

                const char *PROMPT = "go: ";
                g_tui->input_prompt_len = strlen(PROMPT);
                strncpy(g_tui->input_prompt, PROMPT, g_tui->input_prompt_len);

                g_tui->input_caret = g_tui->input_len = uri_strlen;
                strncpy(g_tui->input, uri_string, g_tui->input_len + 1);

                g_tui->cb_input_complete = tui_go_from_input;
            } goto exit_block;

            /* 'h' move caret left */
            case 'h':
            {
                if (g_tui->input_caret == 0) continue;
                --g_tui->input_caret;
                tui_cursor_move(x_pos - 1, g_tui->h);
            } goto exit_block;

            /* 'J' next link */
            case 'J':
            case 'n':
                if (!g_pager->link_count) continue;

                g_pager->selected_link_index =
                    (g_pager->selected_link_index + 1) % g_pager->link_count;

                g_tui->input_caret = g_tui->input_len = snprintf(g_tui->input,
                    TUI_INPUT_BUFFER_MAX,
                    "%d",
                    g_pager->selected_link_index);

                // Print the new value
                x_pos = min(1 + g_tui->input_prompt_len, g_tui->w);
                tui_cursor_move(x_pos, g_tui->h);
                tui_sayn(g_tui->input, g_tui->input_len);
                x_pos += g_tui->input_caret - 1;

                goto update_link_peek;

            /* 'K' previous link */
            case 'K':
            case 'p':
                if (!g_pager->link_count) continue;

                --g_pager->selected_link_index;
                if (g_pager->selected_link_index < 0)
                {
                    g_pager->selected_link_index = g_pager->link_count - 1;
                }

                g_tui->input_caret = g_tui->input_len = snprintf(g_tui->input,
                    TUI_INPUT_BUFFER_MAX,
                    "%d",
                    g_pager->selected_link_index);

                // Print the new value
                x_pos = min(1 + g_tui->input_prompt_len, g_tui->w);
                tui_cursor_move(x_pos, g_tui->h);
                tui_sayn(g_tui->input, g_tui->input_len);
                x_pos += g_tui->input_caret - 1;

                goto update_link_peek;

            default: break;
            }

            // Check that we don't overflow the input
            if (g_tui->input_len + 1 >= TUI_INPUT_BUFFER_MAX)
            {
                continue;
            }

            // Only accept digit input
            if (buf < '0' || buf > '9') continue;

            // If we have a zero at the beginning before other numbers, strip
            // it (as it doesn't make sense for it to be there)
            if (g_tui->input_caret == 1 &&
                g_tui->input[0] == '0')
            {
                g_tui->input_caret = 0;
                g_tui->input_len = 0;
                --x_pos;
                tui_cursor_move(x_pos, g_tui->h);
            }

            g_tui->input[g_tui->input_caret] = buf;

            // Add the text to the buffer
            g_tui->input[g_tui->input_len + 1] = '\0';

            ++g_tui->input_caret;
            ++g_tui->input_len;

            // Print character
            tui_sayn(&buf, 1);

            // Clear old link name that was there
        update_link_peek:
            tui_cursor_move(x_pos + 1, g_tui->h);
            tui_printf("%*s", g_tui->w - x_pos, "");

            if (g_tui->input_len)
            {
                g_pager->selected_link_index = atoi(g_tui->input);
                if (g_pager->selected_link_index < g_pager->link_count)
                {
                    char uri_name[g_tui->w];
                    size_t uri_name_len = uri_str(
                        &g_pager->links[g_pager->selected_link_index].uri,
                        uri_name,
                        g_tui->w, URI_FLAGS_NONE);

                    // Print name of link that number corresponds to
                    tui_cursor_move(x_pos + 1, g_tui->h);
                    tui_printf(" (%.*s)", (int)uri_name_len, uri_name);
                }
                invalidate = true;
            }
            tui_cursor_move(x_pos + 1, g_tui->h);
        }

    exit_block:
        // Repaint the TUI
        if (invalidate)
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

        if (next_mode)
        {
            g_tui->mode = next_mode;

            switch(next_mode)
            {
            // Regular paging mode
            default:
            case TUI_MODE_NORMAL:
                // Hide cursor, etc.
                tui_cursor_move(0, 0);
                tui_say("\x1b[?25l");
                break;

            // Command input mode
            case TUI_MODE_COMMAND:
                // TODO not implemented
                break;

            // Text input mode (also used for link input)
            case TUI_MODE_INPUT:
            case TUI_MODE_LINKS:
                // Write the prompt message and show cursor
                tui_cmd_status_prepare();
                tui_sayn(g_tui->input_prompt, g_tui->input_prompt_len);
                tui_say("\x1b[?25h");

                if (g_tui->input_caret)
                {
                    tui_sayn(g_tui->input, g_tui->input_caret);
                }
                break;

            // Password input mode
            case TUI_MODE_PASSWORD:
                // TODO not implemented
                break;
            }

            if (next_mode == TUI_MODE_LINKS)
            {
                next_mode = TUI_MODE_UNKNOWN;
                goto links_mode_begin;
            }
            next_mode = TUI_MODE_UNKNOWN;
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
    tui_repaint();
}

/* Repaint entire screen */
void
tui_repaint(void)
{
    tui_cursor_move(0, 0);
    tui_say("\x1b[2J");
    pager_paint();
    status_line_paint();
}

/* Goto site that is currently in the input buffer */
static void
tui_go_from_input(void)
{
    // Parse the URI.
    size_t input_len = strlen(g_tui->input);
    struct uri uri = uri_parse(g_tui->input, input_len);

    // We need to make sure that there is a protocol or else the URI parse
    // will cause problems
    if (uri.protocol == PROTOCOL_NONE)
    {
        // Re-parse it with a 'gemini://' prefix
        char tmp[input_len + 1];
        strncpy(tmp, g_tui->input, input_len);
        input_len = snprintf(g_tui->input, TUI_INPUT_BUFFER_MAX,
            "gemini://%s", tmp);
        uri = uri_parse(g_tui->input, input_len);
    }
    tui_go_to_uri(&uri);
}

/* Goto a site */
void
tui_go_to_uri(struct uri *uri)
{
    tui_cmd_status_prepare();

    if (uri->protocol == PROTOCOL_UNKNOWN ||
        uri->protocol == PROTOCOL_GOPHER ||
        uri->protocol == PROTOCOL_FINGER)
    {
        // Show error message
        tui_say("Unsupported protocol");
        return;
    }

    // Assume Gemini if no scheme is given
    if (uri->protocol == PROTOCOL_NONE)
    {
        uri->protocol = PROTOCOL_GEMINI;
    }

    if (uri->hostname[0] == '\0')
    {
        tui_say("Invalid URI");
        return;
    }

    // Show status message
    tui_printf("Loading %s ...", g_tui->input);

    // Attempt to connect to site
    switch (uri->protocol)
    {
    case PROTOCOL_GEMINI:
    {
        gemini_request(uri);
    } break;

    default: break;
    }
}

/* Clear the command line/output TUI */
static void
tui_clear_cmd(void)
{
    tui_cursor_move(0, g_tui->h);

    // This little trick prints repeated spaces for a given count :D
    tui_printf("%*s", g_tui->w, "");
}

/* Prepare the command status line for writing (clear it, etc.) */
void
tui_cmd_status_prepare(void)
{
    tui_clear_cmd();
    tui_cursor_move(0, g_tui->h);
}
