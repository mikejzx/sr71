#include "pch.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_input_prompt.h"
#include "uri.h"

static char s_tmpbuf[TUI_INPUT_BUFFER_MAX];

static enum tui_status buffer_insert(const char *, const ssize_t);
static enum tui_status buffer_backspace(void);
static enum tui_status buffer_backspace_word(void);
static enum tui_status buffer_delete_to_end(void);
static enum tui_status buffer_caret_shift(int);
static enum tui_status buffer_caret_shift_word(int, bool);
static enum tui_status buffer_protocol_cycle(void);

// Whether the current input prompt should not echo chars in plain text
static bool is_sensitive = false;

/* Move cursor to the caret position */
static inline void
buffer_caret_update(void)
{
    tui_cursor_move(g_in->prompt_len + 1 + g_in->caret, g_tui->h);
}

void
tui_input_prompt_begin(
    enum tui_input_mode mode,
    const char *prompt,
    size_t prompt_len,
    const char *default_buffer,
    void(*cb_complete)(void))
{
    g_tui->in_prompt = true;
    tui_status_begin();

    is_sensitive = (mode == TUI_MODE_INPUT_SECRET);

    if (prompt)
    {
        // Print the prompt
        g_in->prompt_len = prompt_len ? prompt_len : strlen(prompt);
        strncpy(g_in->prompt, prompt, g_in->prompt_len + 1);
        tui_say(g_in->prompt);
    }
    else
    {
        g_in->prompt_len = 0;
        *g_in->prompt = '\0';
    }

    if (default_buffer && !is_sensitive)
    {
        // Apply default buffer if given
        g_in->buffer_len = strlen(default_buffer);
        g_in->caret = g_in->buffer_len;
        strncpy(g_in->buffer, default_buffer, TUI_INPUT_BUFFER_MAX);

        // Print the buffer
        tui_say(default_buffer);
    }
    else
    {
        // Reset buffer
        g_in->buffer_len = 0;
        g_in->caret = 0;
        *g_in->buffer = '\0';
    }

    g_in->cb_complete = cb_complete;

    tui_status_end();

    // Show cursor
    tui_say("\x1b[?25h");

    // Switch mode
    tui_input_mode(mode);
}

void
tui_input_prompt_end(enum tui_input_mode mode_to_end)
{
    if (!g_tui->in_prompt || g_in->mode != mode_to_end) return;

    g_tui->in_prompt = false;

    // Hide the cursor
    tui_say("\x1b[?25l");

    // Switch to next mode
    tui_input_mode(TUI_MODE_NORMAL);

    // Execute completion callback
    if (g_in->buffer_len &&
        g_in->cb_complete) g_in->cb_complete();
}

/* Handle simple text input to the buffer */
enum tui_status
tui_input_prompt_text(const char *buf, ssize_t buf_len)
{
    switch(*buf)
    {
    /* Esc: exit the prompt */
    case '\x1b':
        g_in->buffer_len = 0;
    /* Ret and Esc: end of input */
    case '\n':
        tui_input_prompt_end(g_in->mode);
        return TUI_OK;

    /* Handle backspaces */
    case 0x7f: return buffer_backspace();

    /* Ctrl-W: backspace one 'word' */
    case (0100 ^ 'W'): return buffer_backspace_word();

    /* Ctrl-N/Ctrl-P next/previous recent URI entry */
    //case (0100 ^ 'N'): return buffer_history_next(BUFFER_HISTORY_URI);
    //case (0100 ^ 'P'): return buffer_history_next(BUFFER_HISTORY_URI);

    /*
     * Handle caret movement
     * - Ctrl+H, Ctrl+L, and left/right arrow keys for left/right movement
     */
    case (0100 ^ 'H'): return buffer_caret_shift(-1);
    case (0100 ^ 'L'): return buffer_caret_shift(1);

    /*
     * Ctrl-B and Ctrl-F for moving by 'word', was going to use Ctrl-J and
     * Ctrl-K but it causes issues...
     */
    case (0100 ^ 'B'): return buffer_caret_shift_word(-1, true);
    case (0100 ^ 'F'): return buffer_caret_shift_word(1, true);

    /* Ctrl-I: bit of a crazy experimental one; this acts like a 'ciw' in Vim */
    case (0100 ^ 'D'):
    case (0100 ^ 'I'):
        buffer_caret_shift_word(1, true);
        if (g_in->buffer[g_in->caret - 1] == '/') buffer_caret_shift(-1);
        return buffer_backspace_word();

    /* Ctrl-E: experimental 'delete to end' */
    //case (0100 ^ 'C'):
    case (0100 ^ 'E'): return buffer_delete_to_end();

    /*
     * This is here for now; Ctrl-P to cycle through protocols of current URI
     */
    case (0100 ^ 'P'): return buffer_protocol_cycle();

    default: break;
    }

    return buffer_insert(buf, buf_len);
}

/* Handle digit-only input to the buffer */
enum tui_status
tui_input_prompt_digit(const char *buf, ssize_t buf_len)
{
    switch(*buf)
    {
    /* Esc: exit the prompt */
    case '\x1b':
    case 'q':
        g_in->buffer_len = 0;
        tui_input_prompt_end(g_in->mode);
        tui_status_clear();
        return TUI_OK;

    /* Handle backspaces */
    case 0x7f: return buffer_backspace();

    /* 'x' to remove character under cursor */
    case (0100 ^ 'X'):
        buffer_caret_shift(1);
        return buffer_backspace();

    /* Ctrl-W usually just clears the whole thing */
    case (0100 ^ 'W'): return buffer_backspace_word();

    // Caret
    case (0100 ^ 'H'): return buffer_caret_shift(-1);
    case (0100 ^ 'L'): return buffer_caret_shift(1);

    // Only allow inserting digits
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
        return buffer_insert(buf, 1);
    }

    return TUI_UNHANDLED;
}


/* Input prompt for single-char/register inputs */
enum tui_status
tui_input_prompt_register(const char *buf, ssize_t buf_len)
{
    // This is super simple; we just take the first character

    // Cancel on esc
    if (*buf == '\x1b') goto end;

    g_in->buffer[0] = *buf;
    g_in->buffer[1] = '\0';
    g_in->buffer_len = 1;

end:
    tui_status_clear();
    tui_input_prompt_end(g_in->mode);
    return TUI_OK;
}

/*
 * Input prompt for yes/no question inputs.
 * Very similar to the register input function
 */
enum tui_status
tui_input_prompt_yesno(const char *buf, ssize_t buf_len)
{
    g_in->buffer[0] = '\0';
    g_in->buffer_len = 0;

    tui_status_clear();
    tui_input_prompt_end(g_in->mode);

    // Doesn't get much simpler than this
    if (*buf == 'y' || *buf == 'Y') if (g_in->cb_complete) g_in->cb_complete();

    return TUI_OK;
}

/* Insert text into text input buffer at caret pos */
static enum tui_status
buffer_insert(const char *buf, const ssize_t buf_len)
{
    if (g_in->buffer_len + buf_len + 1 >= TUI_INPUT_BUFFER_MAX)
    {
        // No more space in buffer
        return TUI_OK;
    }

    // Copy text after caret into a temporary buffer
    ssize_t tmp_len = g_in->buffer_len - g_in->caret;
    if (tmp_len > 0)
    {
        strncpy(s_tmpbuf,
            g_in->buffer + g_in->caret,
            tmp_len);
    }

    // Insert the new string into the buffer
    strncpy(g_in->buffer + g_in->caret,
        buf,
        buf_len);

    // Append the old string
    if (tmp_len > 0)
    {
        strncpy(g_in->buffer + g_in->caret + buf_len,
            s_tmpbuf,
            tmp_len);
    }

    // Set new null-terminator
    g_in->buffer_len += buf_len;
    g_in->buffer[g_in->buffer_len] = '\0';

    // Print new string section
    tui_status_begin_soft();
    buffer_caret_update();

    if (!is_sensitive)
    {
        // Echo the new buffer
        tui_printf("%s", g_in->buffer + g_in->caret);
    }
    else
    {
        // Echo asterisks
        tui_printf("%*s", g_in->buffer_len - g_in->caret, "*");
    }

    tui_status_end();

    // Update caret position
    g_in->caret += buf_len;
    buffer_caret_update();

    return TUI_OK;
}

/* Backspace N chars from caret pos in input buffer */
static enum tui_status
buffer_backspace(void)
{
    // Can't backspace any more
    if (g_in->caret <= 0) return TUI_OK;

    /* Shift string after deleted char leftwards */
    ssize_t section_len = g_in->buffer_len - g_in->caret;
    if (section_len > 0)
    {
        strncpy(s_tmpbuf,
            g_in->buffer + g_in->caret,
            section_len);
        strncpy(g_in->buffer + g_in->caret - 1,
            s_tmpbuf,
            section_len);
        g_in->buffer[g_in->buffer_len - 1] = '\0';
    }
    else
    {
        // We are at the end of the string; now move the null-terminator
        // back
        g_in->buffer[g_in->buffer_len - 1] = '\0';
    }
    --g_in->buffer_len;

    // Move caret backward
    --g_in->caret;

    // Re-print the text that comes after the caret
    tui_status_begin_soft();
    buffer_caret_update();
    if (g_in->caret < g_in->buffer_len)
    {
        if (!is_sensitive)
        {
            // Echo the new buffer
            tui_printf("%s", g_in->buffer + g_in->caret);
        }
        else
        {
            // Echo asterisks
            tui_printf("%*s", g_in->buffer_len - g_in->caret, "*");
        }
    }
    tui_say(" ");
    buffer_caret_update();
    tui_status_end();

    return TUI_OK;
}

/* Backspace a "word" in the input buffer */
static enum tui_status
buffer_backspace_word(void)
{
    // Can't backspace any more
    if (g_in->caret <= 0) return TUI_OK;

    if (is_sensitive)
    {
        //return buffer_clear();
        g_in->buffer_len = 0;
        g_in->caret = 0;
        g_in->buffer[g_in->buffer_len] = '\0';
        tui_input_prompt_redraw_full();
        return TUI_OK;
    }

    /* Store the string that is to the left of last-deleted char */
    ssize_t section_len = g_in->buffer_len - g_in->caret;
    if (section_len > 0)
    {
        strncpy(s_tmpbuf,
            g_in->buffer + g_in->caret,
            section_len);
    }

    // Move caret backward until we find a nice spot; takes a somewhat similar
    // approach to w3m's input fields (eliminate whole directories at at time)
    int dist = g_in->caret;
    bool is_path = g_in->buffer[g_in->caret - 1] == '/';
    if (isalpha(g_in->buffer[g_in->caret - 1]) || is_path)
    {
        if (is_path)
        {
            --g_in->caret;
            --g_in->buffer_len;
        }

        // Move back until we find non-alphabetic char
        for (;
            g_in->caret > 0 && isalpha(g_in->buffer[g_in->caret - 1]);
            --g_in->caret, --g_in->buffer_len);
    }
    else
    {
        // Move back until we find alphabetic char
        for (;
            g_in->caret > 0 && !isalpha(g_in->buffer[g_in->caret - 1]);
            --g_in->caret, --g_in->buffer_len);
    }
    dist -= g_in->caret;

    if (section_len > 0)
    {
        strncpy(g_in->buffer + g_in->caret,
            s_tmpbuf,
            section_len);
    }
    g_in->buffer[g_in->buffer_len] = '\0';

    // Re-print the text that comes after the caret
    tui_status_begin_soft();
    buffer_caret_update();
    if (g_in->caret < g_in->buffer_len)
    {
        tui_printf("%s",
            g_in->buffer + g_in->caret);
    }
    tui_printf("%*s", dist, "");
    buffer_caret_update();
    tui_status_end();

    return TUI_OK;
}

static enum tui_status
buffer_delete_to_end(void)
{
    int diff = g_in->buffer_len - g_in->caret;

    g_in->buffer_len = g_in->caret;
    g_in->buffer[g_in->buffer_len] = '\0';

    tui_status_begin_soft();
    tui_printf("%*s", diff, "");
    tui_status_end();

    buffer_caret_update();

    return TUI_OK;
}

/* Shift buffer caret by 'n' chars */
static enum tui_status
buffer_caret_shift(int n)
{
    g_in->caret = max(min((int)g_in->caret + n, g_in->buffer_len), 0);
    buffer_caret_update();

    return TUI_OK;
}

/* Shift buffer caret by 'n' words */
static enum tui_status
buffer_caret_shift_word(int n, bool skip_first)
{
    int n_abs = abs(n), dir = sign(n);

    if (is_sensitive)
    {
        // Sensitive just skips to begin/end of input
        if (dir > 0) g_in->caret = g_in->buffer_len - 1;
        else g_in->caret = 0;

        return TUI_OK;
    }

    bool is_path = false;
    for (int x = 0; x < n_abs; ++x)
    {
        if (skip_first) is_path = !isalpha(g_in->buffer[g_in->caret - 1]);

        if (isalpha(g_in->buffer[g_in->caret - 1]) || is_path)
        {
            if (is_path && skip_first)
            {
                g_in->caret += dir;
            }

            // Move back until we find non-alphabetic char
            for (;
                ((g_in->caret > 0 && dir < 0) ||
                (g_in->caret < g_in->buffer_len && dir > 0)) &&
                isalpha(g_in->buffer[g_in->caret - 1]);
                g_in->caret += dir);
        }
        else
        {
            // Move back until we find alphabetic char
            for (;
                ((g_in->caret > 0 && dir < 0) ||
                (g_in->caret < g_in->buffer_len && dir > 0)) &&
                !isalpha(g_in->buffer[g_in->caret - 1]);
                g_in->caret += dir);
        }
    }

    buffer_caret_update();
    return TUI_OK;
}

enum tui_status
tui_input_prompt_redraw_full(void)
{
    if (!g_tui->in_prompt) return TUI_OK;

    tui_status_clear();

    // Print the full status
    tui_status_begin();

    if (!is_sensitive)
    {
        tui_printf("%s%s", g_in->prompt, g_in->buffer);
    }
    else
    {
        tui_printf("%s", g_in->prompt);
        if (g_in->buffer_len > 0)
        {
            tui_printf("%*s", g_in->buffer_len, "*");
        }
    }

    tui_status_end();

    // Update caret
    buffer_caret_update();

    // Special case for link mode; need to update the link preview in status
    if (g_in->mode == TUI_MODE_LINKS) tui_update_link_peek();

    return TUI_OK;
}

/* Cycle through protocol if there is one at beginning of buffer */
static enum tui_status
buffer_protocol_cycle(void)
{
    if (is_sensitive) return TUI_OK;

    // Protocols that we allow cycling through
    static const char *PROTOCOL_SWITCH_AVAILABLE[] =
    {
        PROTOCOL_INFOS[PROTOCOL_GEMINI].name,
        PROTOCOL_INFOS[PROTOCOL_GOPHER].name,
    };
    static const size_t PROTOCOL_SWITCH_AVAILABLE_COUNT =
        sizeof(PROTOCOL_SWITCH_AVAILABLE) / sizeof(const char *);

    for (int p = 0; p < PROTOCOL_SWITCH_AVAILABLE_COUNT; ++p)
    {
        // Check if we have a supported protocol in the buffer
        const size_t plen = strlen(PROTOCOL_SWITCH_AVAILABLE[p]);
        if (g_in->buffer_len < plen + 3 ||
            strncmp(
                g_in->buffer, PROTOCOL_SWITCH_AVAILABLE[p], plen) != 0 ||
            strncmp(
                g_in->buffer + plen, "://", 3) != 0)
        {
            continue;
        }

        // Copy everything to the right of the protocol to the temporary
        // buffer
        ssize_t tmp_len =
            max(min(g_in->buffer_len - plen, sizeof(s_tmpbuf)), 0);
        if (tmp_len > 0)
        {
            strncpy(s_tmpbuf,
                g_in->buffer + plen,
                tmp_len);
        }

        // Write in the next supported protocol
        const char *p_next = PROTOCOL_SWITCH_AVAILABLE[(p + 1) %
            PROTOCOL_SWITCH_AVAILABLE_COUNT];
        size_t p_next_len = strlen(p_next);
        strncpy(g_in->buffer, p_next, sizeof(g_in->buffer));

        int buffer_len_diff =
            (int)p_next_len + (int)tmp_len - (int)g_in->buffer_len;

        g_in->buffer_len = p_next_len + tmp_len;

        if (tmp_len > 0)
        {
            // Restore old suffix text
            //strncpy(g_in->buffer + plen, s_tmpbuf, tmp_len);
            strcat(g_in->buffer, s_tmpbuf);
        }

        g_in->buffer[g_in->buffer_len] = '\0';

        // Move cursor to relative position
        g_in->caret += buffer_len_diff;

        // Re-draw the text in status
        tui_input_prompt_redraw_full();

        break;
    }

    return TUI_OK;
}
