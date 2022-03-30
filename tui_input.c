#include "pch.h"
#include "history.h"
#include "state.h"
#include "tui.h"
#include "uri.h"
#include "pager.h"
#include "cache.h"

static enum tui_invalidate_flags s_invalidate;
static enum tui_mode_id s_next_mode = TUI_MODE_UNKNOWN;

static int tui_handle_mode_normal(char);
static int tui_handle_mode_input(char);
static int tui_handle_mode_links(char);

enum single_char_mode
{
    SMODE_NONE = 0,
    SMODE_MARK_SET,
    SMODE_MARK_FOLLOW,
} s_next_char = SMODE_NONE;

int
tui_handle_input(char buf)
{
    static int result;
    s_invalidate = INVALIDATE_NONE;

    // Marks are handled a bit strangely for the moment; they're seperate from
    // the main state machine.  The input code needs to be re worked quite a bit
    // for a better cleaner implementation...
    if (s_next_char != SMODE_NONE)
    {
        tui_status_clear();

        if (buf == '\n' ||
            buf == '\x1b' ||
            !is_alphanumeric(buf))
        {
            result = 0;
            s_next_char = SMODE_NONE;
            return 0;
        }

        int index;
        if (buf <= '9') index = buf - '0';
        else if (buf <= 'Z') index = buf - 'A' + '9' - '0' + 1;
        else index = buf - 'a' + '9' - '0' + 1 + 'Z' - 'A' + 1;

        switch(s_next_char)
        {
        case SMODE_MARK_SET:
            g_pager->marks[index] = g_pager->scroll;
            break;
        case SMODE_MARK_FOLLOW:
            g_pager->scroll = g_pager->marks[index];
            tui_invalidate(
                INVALIDATE_PAGER_BIT | INVALIDATE_STATUS_LINE_BIT);
            break;
        default: break;
        }
        s_next_char = SMODE_NONE;

        return 0;
    }

    switch(g_tui->mode)
    {
    default:
    case TUI_MODE_NORMAL:
        if (buf == '\n')
        {
            result = 0;
            break;
        }
        result = tui_handle_mode_normal(buf);
        break;

    case TUI_MODE_COMMAND:
        // Not implemented
        exit(0);
        break;

    case TUI_MODE_INPUT:
        result = tui_handle_mode_input(buf);
        break;

    case TUI_MODE_PASSWORD:
        // Not implemented
        exit(0);
        break;

    case TUI_MODE_LINKS:
    links_mode_begin:
        result = tui_handle_mode_links(buf);
        break;
    }

    if (result < 0) return result;

    // Repaint the TUI
    if (s_invalidate) tui_invalidate(s_invalidate);

    // Transition to next mode
    if (!s_next_mode) return 0;
    g_tui->mode = s_next_mode;

    switch(s_next_mode)
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
        tui_status_begin();
        tui_sayn(g_tui->input_prompt, g_tui->input_prompt_len);
        tui_say("\x1b[?25h");
        tui_status_end();

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

    if (s_next_mode == TUI_MODE_LINKS)
    {
        s_next_mode = TUI_MODE_UNKNOWN;
        goto links_mode_begin;
    }
    s_next_mode = TUI_MODE_UNKNOWN;

    return 0;
}

/* Main motion keys */
static bool
tui_handle_common_movement(char buf)
{
    switch (buf)
    {
    // The basic vi/less style keys
    case 'j':
    case ('E' ^ 0100):
        pager_scroll(1);
        s_invalidate = INVALIDATE_ALL;
        return true;
    case 'k':
    case ('Y' ^ 0100):
        pager_scroll(-1);
        s_invalidate = INVALIDATE_ALL;
        return true;
    case 'd':
    case ('D' ^ 0100):
        pager_scroll(g_tui->h / 2);
        s_invalidate = INVALIDATE_ALL;
        return true;
    case 'u':
    case ('U' ^ 0100):
        pager_scroll(g_tui->h / -2);
        s_invalidate = INVALIDATE_ALL;
        return true;
    case ('F' ^ 0100):
        pager_scroll(g_tui->h);
        s_invalidate = INVALIDATE_ALL;
        return true;
    case ('B' ^ 0100):
        pager_scroll(-g_tui->h);
        s_invalidate = INVALIDATE_ALL;
        return true;
    case 'g':
        pager_scroll_top();
        s_invalidate = INVALIDATE_ALL;
        return true;
    case 'G':
        pager_scroll_bottom();
        s_invalidate = INVALIDATE_ALL;
        return true;

    // Paragraph/heading movement
    case '{':
        pager_scroll_paragraph(-1);
        s_invalidate = INVALIDATE_ALL;
        return true;
    case '}':
        pager_scroll_paragraph(1);
        s_invalidate = INVALIDATE_ALL;
        return true;
    case '[':
        pager_scroll_heading(-1);
        s_invalidate = INVALIDATE_ALL;
        return true;
    case ']':
        pager_scroll_heading(1);
        s_invalidate = INVALIDATE_ALL;
        return true;

    // Mark handling (m to set, ' to follow)
    case 'm':
        s_next_char = SMODE_MARK_SET;
        tui_status_say("set mark?");
        return true;
    case '\'':
        s_next_char = SMODE_MARK_FOLLOW;
        tui_status_say("follow mark?");
        return true;

    // Not handled
    default: return false;
    }
}

static int
tui_handle_mode_normal(char buf)
{
    if (tui_handle_common_movement(buf)) return 0;

    // Handle input
    switch (buf)
    {
    case '\x1b':
        // Deselect selected link
        g_pager->selected_link_index = -1;
        s_invalidate = INVALIDATE_PAGER_SELECTED_BIT;
        break;

    // 'o': open page.
    case 'o':
    {
        // Prompt user for URL to visit
        s_next_mode = TUI_MODE_INPUT;

        const char *PROMPT = "go: ";
        g_tui->input_prompt_len = strlen(PROMPT);
        strncpy(g_tui->input_prompt, PROMPT,
            g_tui->input_prompt_len + 1);

        const char *INPUT_DEFAULT = "gemini://";
        g_tui->input_len = strlen(INPUT_DEFAULT);
        g_tui->input_caret = g_tui->input_len;
        strncpy(g_tui->input, INPUT_DEFAULT,
            g_tui->input_len + 1);

        g_tui->cb_input_complete = tui_go_from_input;
    } break;

    // 'f' follow selected link
    case 'f':
    {
        // Make sure we have a link to follow
        if (g_pager->selected_link_index < 0 &&
            g_pager->selected_link_index >= g_pager->link_count)
        {
            break;
        }

        // Find the URI that link corresponds to, and follow
        struct uri *uri =
            &g_pager->links[g_pager->selected_link_index].uri;
        tui_go_to_uri(uri, true, false);
    } break;

    // 'e' to edit current page link in a 'go' command
    case 'e':
    {
        struct uri *uri;
        if (g_pager->selected_link_index < 0 ||
            g_pager->selected_link_index >= g_pager->link_count)
        {
            // Use current page link
            uri = &g_state->uri;
        }
        else
        {
            // Use selected link
            uri = &g_pager->links[g_pager->selected_link_index].uri;
        }
        char uri_string[256];
        size_t uri_strlen =
            uri_str(uri, uri_string, sizeof(uri_string), 0);

        s_next_mode = TUI_MODE_INPUT;

        const char *PROMPT = "go: ";
        g_tui->input_prompt_len = strlen(PROMPT);
        strncpy(g_tui->input_prompt, PROMPT,
            g_tui->input_prompt_len + 1);

        g_tui->input_caret = g_tui->input_len = uri_strlen;
        strncpy(g_tui->input, uri_string, g_tui->input_len + 1);

        g_tui->cb_input_complete = tui_go_from_input;
    } break;

    // ',' navigate history backward
    case ',':
    case 'b':
    {
        const struct history_item *const item = history_pop();
        if (item == NULL || !item->initialised)
        {
            tui_status_say("No older history");
            break;
        }

        tui_go_to_uri(&item->uri, false, false);
    } break;

    // ';' navigate history forward
    case ';':
    case 'w':
    {
        const struct history_item *const item = history_next();
        if (item == NULL || !item->initialised)
        {
            tui_status_say("No re-do history");
            break;
        }

        tui_go_to_uri(&item->uri, false, false);
    } break;

    // 'Ctrl+P' go to parent directory
    case (0100 ^ 'P'):
    case '.':
    {
        struct uri uri_rel =
        {
            .protocol = PROTOCOL_NONE,
            .path = "../",
        };
        uri_abs(&g_state->uri, &uri_rel);
        tui_go_to_uri(&uri_rel, true, false);
    } break;

    // 'Ctrl+R' go to root directory
    case (0100 ^ 'R'):
    {
        struct uri uri;
        memcpy(&uri, &g_state->uri, sizeof(struct uri));
        strcpy(uri.path, "/");
        tui_go_to_uri(&uri, true, false);
    } break;

    // 'r' refresh page
    case 'r':
    {
    #ifdef CACHE_USE_DISK
        // Copy the old hash
        unsigned char old_hash[EVP_MAX_MD_SIZE];
        unsigned old_hash_len = 0;
        if (g_pager->cached_page &&
            uri_cmp(&g_pager->cached_page->uri, &g_state->uri) == 0)
        {
            old_hash_len = g_pager->cached_page->hash_len;
            memcpy(old_hash, g_pager->cached_page->hash, old_hash_len);
        }

        if (tui_go_to_uri(&g_state->uri, false, true) == 0 && old_hash_len)
        {
            // Check if the hashes match
            if (old_hash_len == g_pager->cached_page->hash_len &&
                memcmp(old_hash, g_pager->cached_page->hash, old_hash_len) == 0)
            {
                tui_status_say("no changes since last cache");
            }
            else
            {
                tui_status_say("page has changed since last cache");
            }
        }
    #else
        tui_go_to_uri(&g_state->uri, false, true);
    #endif
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

        s_next_mode = TUI_MODE_LINKS;

        // Format of the prompt is:
        // 'follow link X ? (scheme://the.url/that/link/corresponds/to)
        const char *PROMPT = "follow link ";
        g_tui->input_prompt_len = strlen(PROMPT);
        strncpy(g_tui->input_prompt, PROMPT,
            g_tui->input_prompt_len + 1);

        // We reset input here; though since on mode change we goto
        // links_mode_begin label, it will automatically get added
        g_tui->input_caret = 0;
        g_tui->input_len = 0;

        // This is for 'J' and 'K' and simply sets the selected link to
        // be the first one *on the visible screen*, rather than the
        // first in the entire buffer.  We have to offset it by one (as
        // the 'J' and 'K' handlers in the link mode will add/subtract
        // one link respectively).  Bit hacky but it works
        if ((buf == 'J' || buf == 'n') &&
            g_pager->selected_link_index < 0)
        {
            pager_select_first_link_visible();
            --g_pager->selected_link_index;
        }
        if ((buf == 'K' || buf == 'p') &&
            g_pager->selected_link_index < 0)
        {
            pager_select_last_link_visible();
            ++g_pager->selected_link_index;
        }
    } break;

    // For now 'q' to exit
    case 'q':
        //exit(0);
        //break;
        return -1;

    default: break;
    }

    return 0;
}

static int
tui_handle_mode_input(char buf)
{
    // TODO: proper scrolling or something
    int x_pos = min(
        1 + g_tui->input_prompt_len +
        (int)g_tui->input_caret, g_tui->w);
    tui_cursor_move(x_pos, g_tui->h);

    switch (buf)
    {
    /* Ctrl-W: backspace a whole word */
    case (0100 ^ 'W'):
    {
        // We take a similar approach to what w3m does: remove until we reach a
        // non-alphanumeric character
        if (g_tui->input_caret <= 0)
        {
            // Can't backspace any further
            return 0;
        }
        size_t new;
        for (new = g_tui->input_caret - 1;
            new > 0 && is_alphanumeric(g_tui->input[new - 1]);
            --new);
        int diff = max(g_tui->input_caret - new, 0);
        g_tui->input_len -= diff;
        g_tui->input_caret = new;
        g_tui->input[g_tui->input_caret - 1] = '\0';
        tui_cursor_move(x_pos - diff, g_tui->h);
        tui_printf("%*s", diff, "");
        tui_cursor_move(x_pos - diff, g_tui->h);
    } return 0;

    /* Ctrl-P: cycle protocol of URI */
    case (0100 ^ 'P'):
    {
        const size_t LEN = strlen("gopher"), LEN_P = strlen("gopher://");

        // Check for scheme and change them around (luckily 'gopher' and
        // 'gemini' have same string length...)
        if (g_tui->input_len >= LEN_P &&
            strncmp(g_tui->input, "gopher://", LEN_P) == 0)
        {
            // Change the protocol to Gemini
            memcpy(g_tui->input, "gemini", LEN);
            tui_cursor_move(
                (int)min(1 + g_tui->input_prompt_len, g_tui->w),
                g_tui->h);
            tui_say("gemini");

            // Move back to old position
            tui_cursor_move(x_pos, g_tui->h);
            return 0;
        }
        if (g_tui->input_len >= LEN_P &&
            strncmp(g_tui->input, "gemini://", LEN_P) == 0)
        {
            // Change the protocol to gopher
            memcpy(g_tui->input, "gopher", LEN);
            tui_cursor_move(
                (int)min(1 + g_tui->input_prompt_len, g_tui->w),
                g_tui->h);
            tui_say("gopher");

            // Move back to old position
            tui_cursor_move(x_pos, g_tui->h);
            return 0;
        }
    } return 0;

    /* Return key; end of input */
    case '\n':
        // Execute callback
        if (g_tui->input_len &&
            g_tui->cb_input_complete)
        {
            g_tui->cb_input_complete();
        }

        // Change back to normal input
        s_next_mode = TUI_MODE_NORMAL;
        return 0;

    /* Backspace key; move caret backward */
    case 0x7f:
        if (g_tui->input_caret <= 0)
        {
            // Can't backspace any further
            return 0;
        }
        g_tui->input[g_tui->input_caret - 1] = '\0';
        tui_cursor_move(x_pos - 1, g_tui->h);
        tui_say(" ");
        tui_cursor_move(x_pos - 1, g_tui->h);
        --g_tui->input_caret;
        --g_tui->input_len;
        return 0;

    /* Escape key; cancel input */
    case '\x1b':
        tui_status_clear();
        s_next_mode = TUI_MODE_NORMAL;
        return 0;

    /* Caret movement */
    case (0100 ^ 'H'):
        g_tui->input_caret = max((int)g_tui->input_caret - 1, 0);
        return 0;
    case (0100 ^ 'L'):
        g_tui->input_caret = min(
            g_tui->input_caret + 1, g_tui->input_len);
        return 0;
    }

    // Check that we don't overflow the input
    if (g_tui->input_len + 1 >= TUI_INPUT_BUFFER_MAX)
    {
        return 0;
    }

    g_tui->input[g_tui->input_caret] = buf;

    // Add the text to the buffer
    g_tui->input[g_tui->input_len + 1] = '\0';

    ++g_tui->input_caret;
    ++g_tui->input_len;

    tui_sayn(&buf, 1);

    tui_cursor_move(x_pos + 1, g_tui->h);

    return 0;
}

static int
tui_handle_mode_links(char buf)
{
    // Relocate to caret pos
    int x_pos = min(
        1 + g_tui->input_prompt_len +
        (int)g_tui->input_caret, g_tui->w);
    tui_cursor_move(x_pos, g_tui->h);

    if (tui_handle_common_movement(buf)) return 0;

    switch (buf)
    {
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
            tui_go_to_uri(uri, true, false);
        }
        else
        {
            tui_status_say("No such link");
        }

        // Change back to normal input
        s_next_mode = TUI_MODE_NORMAL;
        return 0;

    /* backspace key; delete and move caret backward */
    case 0x7f:
        if (g_tui->input_caret <= 0)
        {
            // Can't backspace any further
            return 0;
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

    /*
     * Escape key (or 'q'): cancel input.  'Q' to cancel input and
     * deselect selected link
     */
    case 'Q':
        g_pager->selected_link_index = -1;
    case '\x1b':
    case 'q':
        tui_status_clear();
        s_next_mode = TUI_MODE_NORMAL;
        return 0;

    /* 'e' to use/edit the link in a 'go' command */
    case 'e':
    {
        if (g_pager->selected_link_index < 0 ||
            g_pager->selected_link_index >= g_pager->link_count)
        {
            return 0;
        }
        struct uri *uri =
            &g_pager->links[g_pager->selected_link_index].uri;
        char uri_string[256];
        size_t uri_strlen =
            uri_str(uri, uri_string, sizeof(uri_string), 0);

        s_next_mode = TUI_MODE_INPUT;

        const char *PROMPT = "go: ";
        g_tui->input_prompt_len = strlen(PROMPT);
        strncpy(g_tui->input_prompt, PROMPT,
            g_tui->input_prompt_len + 1);

        g_tui->input_caret = g_tui->input_len = uri_strlen;
        strncpy(g_tui->input, uri_string, g_tui->input_len + 1);

        g_tui->cb_input_complete = tui_go_from_input;
    } return 0;

    /* 'h' move caret left */
    case 'h':
    {
        if (g_tui->input_caret == 0) return 0;
        --g_tui->input_caret;
        tui_cursor_move(x_pos - 1, g_tui->h);
    } return 0;

    /* 'J' next link */
    case 'J':
    case 'n':
        if (!g_pager->link_count) return 0;

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
        if (!g_pager->link_count) return 0;

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
        return 0;
    }

    // Only accept digit input
    if (buf < '0' || buf > '9') return 0;

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
    tui_status_begin_soft();
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
    }
    tui_cursor_move(x_pos + 1, g_tui->h);
    tui_status_end();

    s_invalidate = INVALIDATE_PAGER_SELECTED_BIT;

    return 0;
}
