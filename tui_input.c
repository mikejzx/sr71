#include "pch.h"
#include "favourites.h"
#include "pager.h"
#include "state.h"
#include "tui.h"
#include "tui_input.h"
#include "tui_input_prompt.h"

#define INCHAR_LINK_NEXT 'l'
#define INCHAR_LINK_PREV 'h'
#define INCHAR_LINK_NEXT_ALT 'a'
#define INCHAR_LINK_PREV_ALT 'x'

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
    [TUI_MODE_UNKNOWN]       = { NULL                         },
    [TUI_MODE_NORMAL]        = { tui_input_normal             },
    [TUI_MODE_COMMAND]       = { NULL                         },
    [TUI_MODE_INPUT]         = { tui_input_prompt_text        },
    [TUI_MODE_INPUT_SECRET]  = { tui_input_prompt_text        },
    [TUI_MODE_LINKS]         = { tui_input_links              },
    [TUI_MODE_MARK_SET]      = { tui_input_prompt_register    },
    [TUI_MODE_MARK_FOLLOW]   = { tui_input_prompt_register    },
    [TUI_MODE_SEARCH]        = { tui_input_prompt_text        },
    [TUI_MODE_YES_NO]        = { tui_input_prompt_yesno       },
    [TUI_MODE_YES_NO_CANCEL] = { tui_input_prompt_yesnocancel },
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
    // u/d (Ctrl+D, Ctrl+U) or Space for half page up/down
    ['d']        = { pager_scroll,  1, MOVEMENT_INPUT_HALF_PAGE },
    [0100 ^ 'D'] = { pager_scroll,  1, MOVEMENT_INPUT_HALF_PAGE },
    [' ']        = { pager_scroll,  1, MOVEMENT_INPUT_HALF_PAGE },
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

    // And a big ol' switch block of the nice other stuff
    switch (c)
    {
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
        tui_refresh_page();
        return TUI_OK;

    // 'R' to issue a full redraw
    case 'R':
        tui_repaint(true);
        return TUI_OK;

    // 'S' to save page to an explicit path (despite it being in cache if disk
    // cache is enabled)
    case 'S':
        tui_input_prompt_begin(
            TUI_MODE_INPUT,
            "Save document as: ", 0,
            NULL,
            tui_save_to_file);
        break;

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
        tui_input_prompt_begin(
            TUI_MODE_MARK_SET,
            "set mark: ", 0,
            NULL,
            tui_set_mark_from_input);
        return TUI_OK;
    case '\'':
        tui_input_prompt_begin(
            TUI_MODE_MARK_FOLLOW,
            "goto mark: ", 0,
            NULL,
            tui_goto_mark_from_input);
        return TUI_OK;

    // '/' or '?' to enter search mode (latter for reverse)
    case '/':
        tui_input_prompt_begin(
            TUI_MODE_SEARCH,
            "/", 0,
            NULL,
            tui_search_start_forward);
        return TUI_OK;
    case '?':
        tui_input_prompt_begin(
            TUI_MODE_SEARCH,
            "?", 0,
            NULL,
            tui_search_start_reverse);
        return TUI_OK;
    /* 'n'/'N' for next/prev search item */
    case 'n': search_next(); return TUI_OK;
    case 'N': search_prev(); return TUI_OK;

    /* H to view history log */
    case 'H':
    {
        struct uri uri = uri_parse(
            URI_INTERNAL_HISTORY,
            strlen(URI_INTERNAL_HISTORY));
        tui_go_to_uri(&uri, true, true);
    } return TUI_OK;

    /* B to view favourites */
    case 'B':
    {
        struct uri uri = uri_parse(
            URI_INTERNAL_FAVOURITES,
            strlen(URI_INTERNAL_FAVOURITES));
        tui_go_to_uri(&uri, true, true);
    } return TUI_OK;

    /* F to toggle favourite */
    case 'F':
    {
    #if TUI_FAVOURITE_TOGGLE
        tui_favourite_toggle();
    #else
        tui_input_prompt_begin(
            TUI_MODE_YES_NO_CANCEL,
            "favourite page? (Y)es, (N)o, (C)ancel", 0,
            NULL,
            tui_favourite_set);
    #endif
    } return TUI_OK;

    /* Keybinds specific to Favourites page */
    case 'D':
    {
        // Make sure we are on favourites page
        if (!favourites_is_viewing()) return TUI_OK;

        // Need a favourite to be selected
        if (!pager_has_link()) return TUI_OK;

        tui_input_prompt_begin(
            TUI_MODE_YES_NO,
            "unfavourite the selected link? (Y/n)", 0,
            NULL,
            tui_favourite_delete_selected);

    } return TUI_OK;

    default: break;
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
    case 'q':
    #if TUI_QUIT_CONFIRMATION
        tui_input_prompt_begin(
            TUI_MODE_YES_NO,
            "quit? (Y/n)", 0,
            NULL,
            tui_quit);
        return TUI_OK;
    #else
        return TUI_QUIT;
    #endif


    /* 'o' to enter a URI */
    case 'o':
        tui_input_prompt_begin(
            TUI_MODE_INPUT,
            "go: ", 0,
            "gemini://",
            tui_go_from_input);
        return TUI_OK;

    /* 'e' to edit current URI */
    case 'e':
    {
        // Get URI to edit
        struct uri *uri;
        if (!pager_has_link())
        {
            // Use current page URI
            uri = &g_state->uri;
        }
        else
        {
            // Use selected link URI
            uri = &g_pager->links[g_pager->link_index].uri;
        }

        char uristr[URI_STRING_MAX];
        uri_str(uri, uristr, sizeof(uristr), 0);
        tui_input_prompt_begin(
            TUI_MODE_INPUT,
            "go: ", 0,
            uristr,
            tui_go_from_input);
    } return TUI_OK;

    // 'f' or Ret to follow selected link
    case 'f':
        tui_follow_selected_link();
        tui_input_prompt_end(TUI_MODE_NORMAL);
        return TUI_OK;

    // Digits to enter link mode
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
    case INCHAR_LINK_NEXT:
    case INCHAR_LINK_PREV:
    case INCHAR_LINK_NEXT_ALT:
    case INCHAR_LINK_PREV_ALT:
        // Skip if no links in the page
        if (!g_pager->link_count) return TUI_OK;

        bool is_digit =
            *buf != INCHAR_LINK_NEXT     && *buf != INCHAR_LINK_PREV &&
            *buf != INCHAR_LINK_NEXT_ALT && *buf != INCHAR_LINK_PREV_ALT;

        char b[2];
        if (is_digit)
        {
            b[0] = *buf;
            b[1] = '\0';
        }

        // Show the prompt
        tui_input_prompt_begin(
            TUI_MODE_LINKS,
            "follow link: ", 0,
            is_digit ? b : NULL,
            NULL);

        // Put the correct index in the buffer
        if (is_digit)
        {
            g_pager->link_index = atoi(buf);
        }
        else
        {
            // Get the link that is at the top/bottom of the page, or increment
            // current index (if there is one)
            if (pager_has_link())
            {
                if (*buf == INCHAR_LINK_NEXT ||
                    *buf == INCHAR_LINK_NEXT_ALT) tui_select_next_link();
                else tui_select_prev_link();
            }
            else if (*buf == INCHAR_LINK_NEXT || *buf == INCHAR_LINK_NEXT_ALT)
            {
                // Select first visible link
                pager_select_first_link_visible();
            }
            else
            {
                // Select last visible link
                pager_select_last_link_visible();
            }

            // We need to manually put the link in the buffer
            g_in->caret =
            g_in->buffer_len = snprintf(
                g_in->buffer,
                sizeof(g_in->buffer),
                "%d",
                g_pager->link_index);
            tui_status_begin_soft();
            tui_say(g_in->buffer);
            tui_cursor_move(g_in->prompt_len + 1 + g_in->caret, g_tui->h);
            tui_status_end();
        }

        tui_update_link_peek();
        return TUI_OK;
    }

    return TUI_UNHANDLED;
}

// Handle link mode input
static enum tui_status
tui_input_links(const char *buf, const ssize_t buf_len)
{
    enum tui_status status = TUI_OK;

    // Apply common handlers
    if (tui_input_common(buf, buf_len) == TUI_OK) return TUI_OK;

    switch(*buf)
    {
    // 'f' or Ret to follow selected link
    case 'f':
    case '\n':
        tui_follow_selected_link();
        tui_input_prompt_end(TUI_MODE_LINKS);
        return TUI_OK;

    // 'e' to edit selected URI
    case 'e':
    {
        struct uri *uri;
        if (!pager_has_link())
        {
            // No selected link
            return TUI_OK;
        }
        uri = &g_pager->links[g_pager->link_index].uri;

        char uristr[URI_STRING_MAX];
        uri_str(uri, uristr, sizeof(uristr), 0);
        tui_input_prompt_begin(
            TUI_MODE_INPUT,
            "go: ", 0,
            uristr,
            tui_go_from_input);
    } return TUI_OK;

    // increment/decrement link index
    case INCHAR_LINK_NEXT:
    case INCHAR_LINK_NEXT_ALT:
        tui_select_next_link();
    case INCHAR_LINK_PREV:
    case INCHAR_LINK_PREV_ALT:
        if (*buf == INCHAR_LINK_PREV || *buf == INCHAR_LINK_PREV_ALT)
        {
            tui_select_prev_link();
        }

        // Write new value into the buffer
        g_in->caret =
        g_in->buffer_len = snprintf(
            g_in->buffer,
            sizeof(g_in->buffer),
            "%d",
            g_pager->link_index);

        // Draw status text
        tui_status_begin_soft();
        tui_cursor_move(g_in->prompt_len + 1, g_tui->h);
        tui_say(g_in->buffer);
        tui_status_end();

        goto no_digit;
    }

    status = tui_input_prompt_digit(buf, buf_len);
    if (status == TUI_UNHANDLED) { return status; }

    // Parse the new link index
    int sel;
    if (g_in->buffer_len)
    {
        if ((sel = atoi(g_in->buffer)) < 0 ||
            sel >= g_pager->link_count)
        {
            sel = -1;
        }
    }
    else
    {
        sel = -1;
    }
    g_pager->link_index = sel;

    // Use this label to skip digit input and parsing
no_digit:

    /* Handle changed link indices and link preview text */
    tui_update_link_peek();

    return status;
}
