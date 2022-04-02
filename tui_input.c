#include "pch.h"
#include "pager.h"
#include "state.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_input_prompt.h"

struct tui_input *g_in;

static enum tui_status tui_input_normal(const char *, const ssize_t);
static enum tui_status tui_input_links(const char *, const ssize_t);

// Input handler list
static const struct tui_input_handler
{
    // Handler function
    const enum tui_status(*func)(const char *, const ssize_t);
} TUI_INPUT_HANDLERS[TUI_MODE_COUNT] =
{
    [TUI_MODE_UNKNOWN]      = { NULL                      },
    [TUI_MODE_NORMAL]       = { tui_input_normal          },
    [TUI_MODE_COMMAND]      = { NULL                      },
    [TUI_MODE_INPUT]        = { tui_input_prompt_text     },
    [TUI_MODE_INPUT_SECRET] = { NULL                      },
    [TUI_MODE_LINKS]        = { tui_input_links           },
    [TUI_MODE_MARK_SET]     = { tui_input_prompt_register },
    [TUI_MODE_MARK_FOLLOW]  = { tui_input_prompt_register },
    [TUI_MODE_SEARCH]       = { NULL                      },
};

/* Special flags for movement functions */
enum movement_input_flag
{
    MOVEMENT_INPUT_NONE = 0,
    MOVEMENT_INPUT_HALF_PAGE,
    MOVEMENT_INPUT_FULL_PAGE,
};

/* Movement functions which can map directly to characters */
static const struct movement_char
{
    void(*func)(int);
    int func_param;
    enum movement_input_flag flag;
} MOVEMENT_CHARS[256] =
{
    // j/k (Ctrl+E, Ctrl+Y) for up/down
    ['j']        = { pager_scroll,  1, 0 },
    [0100 ^ 'E'] = { pager_scroll,  1, 0 },
    ['k']        = { pager_scroll, -1, 0 },
    [0100 ^ 'Y'] = { pager_scroll, -1, 0 },
    // u/d (Ctrl+D, Ctrl+U) for half page up/down
    ['d']        = { pager_scroll,  1, MOVEMENT_INPUT_HALF_PAGE },
    [0100 ^ 'D'] = { pager_scroll,  1, MOVEMENT_INPUT_HALF_PAGE },
    ['u']        = { pager_scroll, -1, MOVEMENT_INPUT_HALF_PAGE },
    [0100 ^ 'U'] = { pager_scroll, -1, MOVEMENT_INPUT_HALF_PAGE },
    // Ctrl+F/Ctrl+B for full page up/down
    [0100 ^ 'F'] = { pager_scroll,  1, MOVEMENT_INPUT_FULL_PAGE },
    [0100 ^ 'B'] = { pager_scroll, -1, MOVEMENT_INPUT_FULL_PAGE },
    // g/G for top/bottom
    ['g']        = { pager_scroll_topbot, -1, 0 },
    ['G']        = { pager_scroll_topbot,  1, 0 },
    // {} for paragraph movement
    ['{']        = { pager_scroll_paragraph, -1, 0 },
    ['}']        = { pager_scroll_paragraph,  1, 0 },
    // [] for heading movement
    ['[']        = { pager_scroll_heading, -1, 0 },
    [']']        = { pager_scroll_heading,  1, 0 },
};

/* Movement escape codes */
static const struct movement_escape
{
    const char *esc;
    struct movement_char m;
} MOVEMENT_ESCAPES[] =
{
    // Home; top of page
    { "\x1b[1~", { pager_scroll_topbot, -1 } },
    // End:  end of page
    { "\x1b[4~", { pager_scroll_topbot,  1 } },
    // PgUp; full page up
    { "\x1b[5~", { pager_scroll, -1, MOVEMENT_INPUT_FULL_PAGE } },
    // PgDn; full page down
    { "\x1b[6~", { pager_scroll,  1, MOVEMENT_INPUT_FULL_PAGE } },

    // TODO: arrow keys

    // Null-terminator
    { NULL },
};

static inline void
movement_char_execute(const struct movement_char *m)
{
    static int i;

    // Get parameter
    i = m->func_param;
    switch(m->flag)
    {
    case MOVEMENT_INPUT_HALF_PAGE: i *= g_tui->h / 2; break;
    case MOVEMENT_INPUT_FULL_PAGE: i *= g_tui->h;     break;
    default: break;
    }

    // Execute function and invalidate the TUI
    m->func(i);
    tui_invalidate(INVALIDATE_ALL);
}

/* Initialise input handler */
void
tui_input_init(void)
{
    g_in = &g_tui->in;
    memset(g_in, 0, sizeof(struct tui_input));
    tui_input_mode(TUI_MODE_NORMAL);
}

/* Handle given input buffer */
enum tui_status
tui_input_handle(const char *buf, const ssize_t buf_len)
{
    // Run input handler function
    enum tui_status(*func)(const char *, const ssize_t) =
        TUI_INPUT_HANDLERS[g_in->mode].func;
    if (!func) return TUI_QUIT;
    return func(buf, buf_len);
}

static enum tui_status
tui_input_common(const char *buf, const ssize_t buf_len)
{
    union
    {
        const struct movement_char *c;
        const struct movement_escape *x;
    } m;

    // First handle escape inputs, which are multi-byte
    if (buf_len > 1)
    {
        // Check if any escapes match this
        for (m.x = MOVEMENT_ESCAPES;
            m.x->esc;
            ++m.x)
        {
            if (buf_len == strlen(m.x->esc) &&
                strncmp(buf, m.x->esc, buf_len) == 0 &&
                m.x->m.func != NULL)
            {
                movement_char_execute(&m.x->m);
                return TUI_OK;
            }
        }

        return TUI_UNHANDLED;
    }

    // Movement handlers
    const char c = *buf;
    if (c >= 0 &&
        c < sizeof(MOVEMENT_CHARS) &&
        (m.c = &MOVEMENT_CHARS[(int)c])->func != NULL)
    {
        movement_char_execute(m.c);
        return TUI_OK;
    }

    return TUI_UNHANDLED;
}

// Handle normal mode input
static enum tui_status
tui_input_normal(const char *buf, const ssize_t buf_len)
{
    // Apply common handlers
    if (tui_input_common(buf, buf_len) == TUI_OK) return TUI_OK;

    // Chars specific to Normal mode
    const char c = *buf;
    switch(c)
    {
    /* 'q' to quit */
    case 'q': return TUI_QUIT;

    /* 'o' to enter a URI */
    case 'o':
        tui_input_prompt_begin("go: ", "gemini://", tui_go_from_input);
        tui_input_mode(TUI_MODE_INPUT);
        return TUI_OK;

    /* 'e' to edit current URI */
    case 'e':
    {
        // Get URI to edit
        struct uri *uri;
        if (g_pager->selected_link_index < 0 ||
            g_pager->selected_link_index >= g_pager->link_count)
        {
            // Use current page URI
            uri = &g_state->uri;
        }
        else
        {
            // Use selected link URI
            uri = &g_pager->links[g_pager->selected_link_index].uri;
        }

        char uristr[URI_STRING_MAX];
        uri_str(uri, uristr, sizeof(uristr), 0);
        tui_input_prompt_begin("go: ", uristr, tui_go_from_input);
        tui_input_mode(TUI_MODE_INPUT);
    } return TUI_OK;

    /* '.' to go to parent directory */
    case '.':
    {
        struct uri uri_parent =
        {
            .protocol = PROTOCOL_NONE,
            .path = "../",
        };
        uri_abs(&g_state->uri, &uri_parent);
        tui_go_to_uri(&uri_parent, true, false);
    } return TUI_OK;

    /* 'Ctrl+R' go to root directory */
    case (0100 ^ 'R'):
    {
        struct uri uri_root;
        memcpy(&uri_root, &g_state->uri, sizeof(struct uri));
        uri_root.path[0] = '/';
        uri_root.path[1] = '\0';
        tui_go_to_uri(&uri_root, true, false);
    } return TUI_OK;

    // 'r' to refresh
    case 'r':
    {
        // TODO
    } return TUI_OK;

    // ',' history backward
    case ',':
    {
        const struct history_item *const item = history_pop();
        if (item == NULL ||
            !item->initialised)
        {
            tui_status_say("Already at oldest page");
            return TUI_OK;
        }
        tui_go_to_uri(&item->uri, false, false);
    } return TUI_OK;

    // ';' history forward
    case ';':
    {
        const struct history_item *const item = history_next();
        if (item == NULL ||
            !item->initialised)
        {
            tui_status_say("Already at latest page");
            return TUI_OK;
        }
        tui_go_to_uri(&item->uri, false, false);
    } return TUI_OK;

    // 'm' mark set mode and apostrophe for mark follow
    case 'm':
        tui_input_prompt_begin("set mark: ", NULL, tui_set_mark_from_input);
        tui_input_mode(TUI_MODE_MARK_SET);
        return TUI_OK;
    case '\'':
        tui_input_prompt_begin("goto mark: ", NULL, tui_goto_mark_from_input);
        tui_input_mode(TUI_MODE_MARK_FOLLOW);
        return TUI_OK;
    }

    return TUI_UNHANDLED;
}

// Handle link mode input
static enum tui_status
tui_input_links(const char *buf, const ssize_t buf_len)
{
    // Apply common handlers
    if (tui_input_common(buf, buf_len) == TUI_OK) return TUI_OK;

    return TUI_UNHANDLED;
}
