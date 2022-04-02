#ifndef TUI_INPUT_H
#define TUI_INPUT_H

/*
 * tui_input.h
 *
 * The input code was just getting so crazy I had to move it all to a separate
 * file
 */

// Max number of characters allowed in the input buffer and prompt
#define TUI_INPUT_BUFFER_MAX 256
#define TUI_INPUT_PROMPT_MAX 32

enum tui_status
{
    TUI_QUIT = -1,
    TUI_OK = 0,
    TUI_UNHANDLED,
};

// TUI input modes
enum tui_input_mode
{
    TUI_MODE_UNKNOWN = 0,

    // Normal mode; scroll around pager, etc.
    TUI_MODE_NORMAL,

    // Prompt: User command input
    // (unused)
    TUI_MODE_COMMAND,

    // Prompt: standard input
    TUI_MODE_INPUT,

    // Prompt: password
    TUI_MODE_INPUT_SECRET,

    // Special mode for following links (when pressing numbers on keyboard).
    // Technically acts as a prompt too.
    TUI_MODE_LINKS,

    // Prompt: mark setting/following
    TUI_MODE_MARK_SET,
    TUI_MODE_MARK_FOLLOW,

    // Prompt: in-buffer search mode
    TUI_MODE_SEARCH,

    TUI_MODE_COUNT
};

struct tui_input
{
    // Current input mode
    enum tui_input_mode mode;

    // Whether we are in an input prompt
    bool is_prompt;

    // User's current input buffer
    char buffer[TUI_INPUT_BUFFER_MAX];

    // The buffer length; this does NOT include the null-termination character
    size_t buffer_len;

    // Location of input buffer caret.
    // This is in range of 0 to buffer length + 1
    size_t caret;

    // Current prompt presented to user
    char prompt[TUI_INPUT_PROMPT_MAX];
    size_t prompt_len;

    // Input complete callback
    void (*cb_complete)(void);
};

extern struct tui_input *g_in;

void tui_input_init(void);
enum tui_status tui_input_handle(const char *, const ssize_t);

static inline void
tui_input_mode(enum tui_input_mode mode)
{
    g_in->mode = mode;
}

#endif
