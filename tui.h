#ifndef TUI_H
#define TUI_H

struct uri;

/*
 * tui.h
 *
 * Ncurses-free text interface management
 */

enum tui_mode_id
{
    TUI_MODE_UNKNOWN = 0,

    // Normal mode; scrolls around pager, etc.
    TUI_MODE_NORMAL,

    // User command input prompt
    TUI_MODE_COMMAND,

    // Input prompt
    TUI_MODE_INPUT,

    // Password prompt
    TUI_MODE_PASSWORD,

    // Special mode for following links (when pressing numbers on keyboard)
    TUI_MODE_LINKS,
};

enum tui_invalidate_flags
{
    INVALIDATE_ALL = -1,
    INVALIDATE_NONE = 0,

    // Refresh the entire pager
    INVALIDATE_PAGER_BIT = 1,

    // Only refresh selected links in the pager (partial update)
    INVALIDATE_PAGER_SELECTED_BIT = 2,

    // Refresh the status line
    INVALIDATE_STATUS_LINE_BIT = 4,
};

// Max number of characters allowed in the input prompt
#define TUI_INPUT_BUFFER_MAX 256
#define TUI_INPUT_PROMPT_MAX 32

struct tui_state
{
    // Terminal width/height
    int w, h;

    int cursor_x, cursor_y;
    bool is_writing_status;

    // Current input mode
    enum tui_mode_id mode;

    // For input prompts
    char input[TUI_INPUT_BUFFER_MAX], input_prompt[TUI_INPUT_PROMPT_MAX];
    size_t input_len;
    size_t input_caret, input_prompt_len;
    void (*cb_input_complete)(void);
};

extern struct tui_state *g_tui;

void tui_init(void);
void tui_cleanup(void);
int  tui_update(void);
void tui_resized(void);
void tui_repaint(bool);
void tui_invalidate(enum tui_invalidate_flags);
int tui_go_to_uri(const struct uri *const, bool, bool);
void tui_go_from_input(void);

/* Move cursor */
#define tui_cursor_move(x, y) \
{ \
    tui_printf("\x1b[%d;%dH", (y), (x)); \
    g_tui->cursor_x = x; \
    g_tui->cursor_y = y; \
}

// Write text to the TUI
#define tui_sayn(x, n) \
do \
{ \
    int size = (n); \
    int l; \
    if (g_tui->is_writing_status && \
        (l = utf8_strnlen_w_formats((x), size)) && \
        g_tui->cursor_x + l >= g_tui->w) \
    { \
        /* Make sure status text doesn't overflow */ \
        l = max(g_tui->w - g_tui->cursor_x - 1, 0); \
        size = utf8_size_w_formats((x), l); \
        if (size < 1) break;\
    } \
    l = write(STDOUT_FILENO, (x), size); \
    if (g_tui->is_writing_status) \
        g_tui->cursor_x += utf8_strnlen_w_formats((x), l); \
} while(0)

#define tui_say(x) tui_sayn((x), sizeof((x)))

// Shorthand for writing simple text to status
#define tui_status_say(x) \
{ \
    tui_status_begin(); \
    tui_say(x); \
    tui_status_end(); \
}

/* Print formatted text to the TUI */
#define tui_printf(...) \
do \
{ \
    static char buf[256]; \
    int count = snprintf(buf, sizeof(buf), __VA_ARGS__); \
    int l; \
    if (g_tui->is_writing_status && \
        (l = utf8_strnlen_w_formats(buf, count)) && \
        g_tui->cursor_x + l >= g_tui->w) \
    { \
        /* Make sure status text doesn't overflow */ \
        l = max(g_tui->w - g_tui->cursor_x - 1, 0); \
        count = utf8_size_w_formats(buf, l); \
        if (count < 1) break; \
    } \
    l = write(STDOUT_FILENO, buf, count); \
    if (g_tui->is_writing_status) \
        g_tui->cursor_x += utf8_strnlen_w_formats(buf, l); \
} while(0)

/* Print a size */
static inline void
tui_print_size(int size)
{
    if (size < 1024) { tui_printf("%d b", size); }
    else if (size < 1024 * 1024) { tui_printf("%.2f KiB", size / 1024.0f); }
    else { tui_printf("%.2f MiB", size / (1024.0f * 1024.0f)); }
}

/* Clear the command line/output TUI */
static inline void
tui_status_clear(void)
{
    tui_cursor_move(0, g_tui->h);

    // This little trick prints repeated spaces for a given count :D
    tui_printf("%*s", g_tui->w, "");
}

/* Begin writing to the command status line */
static inline void
tui_status_begin(void)
{
    tui_status_clear();
    tui_cursor_move(0, g_tui->h);
    g_tui->is_writing_status = true;
}

/* Begin writing to the command status line (without clearing) */
static inline void
tui_status_begin_soft(void)
{
    g_tui->is_writing_status = true;
}

/* Finish writing to command status line */
static inline void
tui_status_end(void)
{
    g_tui->is_writing_status = false;
}

#endif
