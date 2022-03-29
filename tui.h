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
void tui_invalidate(void);
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
    if (g_tui->is_writing_status && \
        size > g_tui->w - g_tui->cursor_x) \
    { \
        /* Make sure status text doesn't overflow */ \
        size = g_tui->w - g_tui->cursor_x; \
    } \
    (void)!write(STDOUT_FILENO, (x), size); \
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
    if (g_tui->is_writing_status && \
        count > g_tui->w - g_tui->cursor_x) \
    { \
        /* Make sure status text doesn't overflow */ \
        count = g_tui->w - g_tui->cursor_x; \
    } \
    (void)!write(STDOUT_FILENO, buf, count); \
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
