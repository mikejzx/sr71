#include "pch.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_input_prompt.h"

static char s_tmpbuf[TUI_INPUT_BUFFER_MAX];

static enum tui_status buffer_insert(const char *, const ssize_t);
static enum tui_status buffer_backspace(void);
static enum tui_status buffer_caret_shift(int);

/* Move cursor to the caret position */
static inline void
buffer_caret_update(void)
{
    tui_cursor_move(g_in->prompt_len + 1 + g_in->caret, g_tui->h);
}

void
tui_input_prompt_begin(
    const char *prompt,
    const char *default_buffer,
    void(*cb_complete)(void))
{
    tui_status_begin();

    if (prompt)
    {
        // Print the prompt
        g_in->prompt_len = strlen(prompt);
        strcpy(g_in->prompt, prompt);
        tui_say(g_in->prompt);
    }
    else
    {
        g_in->prompt_len = 0;
        *g_in->prompt = '\0';
    }

    if (default_buffer)
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
}

void
tui_input_prompt_end()
{
    // Hide the cursor
    tui_say("\x1b[?25l");
}

/* Handle simple text input to the buffer */
enum tui_status
tui_input_prompt_text(const char *buf, ssize_t buf_len)
{
    switch(*buf)
    {
    /* Ret: end of input */
    case '\n':
        // Execute callback
        if (g_in->buffer_len && g_in->cb_complete) g_in->cb_complete();
        // ..fall through

    /* Esc: exit the prompt */
    case '\x1b':
        tui_input_prompt_end();
        tui_input_mode(TUI_MODE_NORMAL);
        return TUI_OK;

    /* Handle backspaces */
    case 0x7f: return buffer_backspace();

    /*
     * Handle caret movement
     * - Ctrl+H, Ctrl+L, and left/right arrow keys for left/right movement
     */
    case (0100 ^ 'H'): return buffer_caret_shift(-1);
    case (0100 ^ 'L'): return buffer_caret_shift(1);

    /* - Ctrl-B, Ctrl-W for next/prev "section" in buffer (e.g. directories) */

    default: break;
    }

    return buffer_insert(buf, buf_len);
}

/* Input prompt for single-char/register inputs */
enum tui_status
tui_input_prompt_register(const char *buf, ssize_t buf_len)
{
    // This is super simple; we just take the first character

    // Cancel on esc
    if (*buf == '\x1b') goto end;

#if 0
    strncpy(g_in->buffer, buf, TUI_INPUT_BUFFER_LEN_MAX);
    g_in->buffer_len += buf_len;
#else
    g_in->buffer[0] = *buf;
    g_in->buffer[1] = '\0';
    g_in->buffer_len = 1;
#endif

    if (g_in->cb_complete) g_in->cb_complete();

end:
    tui_status_clear();
    tui_input_prompt_end();
    tui_input_mode(TUI_MODE_NORMAL);

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

    // Print new string section
    tui_status_begin_soft();
    buffer_caret_update();
    tui_printf("%s", g_in->buffer + g_in->caret);
    tui_status_end();

    // Update caret position
    g_in->caret += buf_len;
    g_in->buffer_len += buf_len;
    buffer_caret_update();

    return TUI_OK;

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
        tui_printf("%s",
            g_in->buffer + g_in->caret);
    }
    tui_say(" ");
    buffer_caret_update();
    tui_status_end();
    return TUI_OK;
}

/* Shift buffer caret left by 'n' chars */
static enum tui_status
buffer_caret_shift(int n)
{
    g_in->caret = max(min((int)g_in->caret + n, g_in->buffer_len), 0);
    buffer_caret_update();

    return TUI_OK;
}
