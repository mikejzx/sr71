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

    // Current input mode
    enum tui_mode_id mode;

    // For input prompts
    char input[TUI_INPUT_BUFFER_MAX], input_prompt[TUI_INPUT_PROMPT_MAX];
    size_t input_len;
    size_t input_caret, input_prompt_len;
    void (*cb_input_complete)(void);

    int cursor_x, cursor_y;
};

extern struct tui_state *g_tui;

#define tui_printf(...) (dprintf(STDOUT_FILENO, __VA_ARGS__))
#define tui_cursor_move(x, y) \
{ \
    tui_printf("\x1b[%d;%dH", (y), (x)); \
    g_tui->cursor_x = x; \
    g_tui->cursor_y = y; \
}
#define tui_say(x) ((void)!write(STDOUT_FILENO, (x), sizeof((x))))
#define tui_sayn(x, l) ((void)!write(STDOUT_FILENO, (x), (l)))

void tui_init(void);
void tui_cleanup(void);
int tui_update(void);
void tui_resized(void);
void tui_repaint(void);
void tui_cmd_status_prepare(void);
void tui_go_to_uri(const struct uri *const, bool);

#endif
