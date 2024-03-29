#include "pch.h"
#include "pager.h"
#include "search.h"
#include "state.h"
#include "status_line.h"
#include "typesetter.h"

#define PAGER_TOP (STATUS_LINE_TOP_HEIGHT)
#define PAGER_HEIGHT \
    max(g_tui->h - STATUS_LINE_TOP_HEIGHT - STATUS_LINE_BOTTOM_HEIGHT, 1)

struct pager_state *g_pager = NULL;

static void pager_alloc_visible_buffer(struct pager_visible_buffer *, int);

static void
pager_recalc_margin(void)
{
    int marg = g_tui->w - CONTENT_WIDTH_PREFERRED;
    g_pager->margin.l = max(0, floor(marg * CONTENT_MARGIN_BIAS));
    g_pager->margin.r = max(0, ceil(marg * (1.0f - CONTENT_MARGIN_BIAS)));
}

void
pager_init(void)
{
    g_pager = &g_state.pager;

    g_pager->visible_buffer.rows = NULL;
    g_pager->visible_buffer_prev.rows = NULL;

    g_pager->link_capacity = 10;
    g_pager->links = malloc(g_pager->link_capacity * sizeof(struct pager_link));

    search_init();

    typesetter_init(&g_pager->typeset);
}

void
pager_update_page(int selected, int scroll)
{
    g_pager->link_count = 0;
    g_pager->link_index = selected;
    g_pager->scroll = scroll;

    // Reset marks
    memset(g_pager->marks, 0, sizeof(g_pager->marks));

    // Reset search
    search_reset();

    // Re-calculate margins
    pager_recalc_margin();

    // Copy page data into the typesetter
    typesetter_reinit(&g_pager->typeset);

    // Typeset the content
    bool typeset = typeset_page(&g_pager->typeset,
        &g_pager->buffer,
        g_pager->visible_buffer.w - g_pager->margin.l - g_pager->margin.r,
        &g_recv->mime);

    // Refresh the TUI
    tui_repaint(false);

    if (!typeset)
    {
        tui_status_begin();
        tui_printf("no mailcap entry for '%s'", g_recv->mime.str);
        tui_status_end();

        // TODO open file with mailcap entry?
    }
}

void
pager_deinit(void)
{
    if (g_pager->links) free(g_pager->links);

    if (g_pager->visible_buffer.rows)
    {
        free(g_pager->visible_buffer.rows);
    }
    if (g_pager->visible_buffer_prev.rows)
    {
        free(g_pager->visible_buffer_prev.rows);
    }
    search_deinit();
    free(g_pager->buffer.b);
    free(g_pager->buffer.lines);
    typesetter_deinit(&g_pager->typeset);
}

void
pager_resized(void)
{
    pager_recalc_margin();

    int content_width =
        g_tui->w - g_pager->margin.l - g_pager->margin.r;
    bool width_changed =
        g_pager->typeset.content_width != content_width;

    // Allocate visible buffers (if not already allocated)
    pager_alloc_visible_buffer(&g_pager->visible_buffer, PAGER_HEIGHT);
    pager_alloc_visible_buffer(&g_pager->visible_buffer_prev, PAGER_HEIGHT);

    // Don't need to do anything else unless width had changed (as width
    // affects word-wrapping, etc.)
    if (!width_changed) return;

    // We need to update search match positions
    search_update();

    // For restoring scroll position after the resize.
    int scroll_raw_index = 0;
    size_t scroll_raw_dist = 0;
    if (g_pager->buffer.line_count)
    {
        scroll_raw_index = g_pager->buffer.lines[g_pager->scroll].raw_index;
        scroll_raw_dist = g_pager->buffer.lines[g_pager->scroll].raw_dist;
    }

    // Re-typeset the content, to update word-wrapping, etc.
    typeset_page(&g_pager->typeset,
        &g_pager->buffer,
        content_width,
        &g_recv->mime);

    // Now we need to restore the scroll position by finding the line that has
    // the raw index we had
    for (int r = 0; r < g_pager->buffer.line_count; ++r)
    {
        if (g_pager->buffer.lines[r].raw_index != scroll_raw_index) continue;

        // Store the top of the wrapped line initially
        g_pager->scroll = r;

        // Call it a day if there was no previous buffer
        if (g_pager->visible_buffer_prev.h == 0) break;

        // Iterate over the rest of the lines with this index.
        struct pager_buffer_line *l;
        for (l = &g_pager->buffer.lines[r];
            l->raw_index == scroll_raw_index;
            l = &g_pager->buffer.lines[r], ++r)
        {
            // Found the line near to where we were before
            if (l->raw_dist >= scroll_raw_dist) break;

            g_pager->scroll = r;
        }
        if (l)
        {
            // Honestly don't know why this even works (and seems to work well
            // too)
            size_t diff = l->raw_dist - scroll_raw_dist;
            if (diff > l->bytes / 2)
            {
                g_pager->scroll = max(g_pager->scroll - 1, 0);
            }
        }
        break;
    }
}

void
pager_scroll(int amount)
{
    if (g_pager->scroll + amount < 0)
    {
        g_pager->scroll = 0;
        return;
    }
    if (g_pager->scroll + amount >= g_pager->buffer.line_count)
    {
        g_pager->scroll = g_pager->buffer.line_count - 1;
        return;
    }
    g_pager->scroll += amount;
}

/* -1 for top, 1 for bottom */
void
pager_scroll_topbot(int t)
{
    g_pager->scroll = t == -1
        ? 0
        : min(max((int)g_pager->buffer.line_count -
                  (int)g_pager->visible_buffer.h / 2, 0),
                       g_pager->buffer.line_count - 1);
}

/* Scroll to next paragraph (dir is -1 or 1) */
void
pager_scroll_paragraph(int dir)
{
    int i;

    // Scroll to next non-empty line (beginning of paragraph)
    for (i = g_pager->scroll;
        i >= 0 &&
            i < g_pager->buffer.line_count &&
            g_pager->buffer.lines[i].bytes <= 1;
        i += dir);

    // Scroll to next empty line (end of paragraph)
    for (;
        i >= 0 &&
            i < g_pager->buffer.line_count &&
            g_pager->buffer.lines[i].bytes > 1;
        i += dir);

    g_pager->scroll = min(max(i, 0), g_pager->buffer.line_count - 1);
}

/* Scroll to next heading (dir is -1 or 1) */
void pager_scroll_heading(int dir)
{
    for (int i = g_pager->scroll + dir;
        i >= 0 && i < g_pager->buffer.line_count;
        i += dir)
    {
        if (g_pager->buffer.lines[i].is_heading)
        {
            g_pager->scroll = max(min(i, g_pager->buffer.line_count - 1), 0);
            return;
        }
    }
}

/*
 * Re-paint the entire pager
 * Pass false to 'full' to only paint a subset (at the moment just paints
 * selected link if it changed)
 */
void
pager_paint(bool full)
{
    bool update_sel_link =
        g_pager->link_index_prev != g_pager->link_index;

    // Partial updates only run if selected link changed
    if (!full && !update_sel_link) return;

    // Draw the visible buffer
    int line_index;
    bool moved, will_print;
    struct pager_buffer_line *line;
    for (int i = 0; i < g_pager->visible_buffer.h; ++i)
    {
        line_index = i + g_pager->scroll;
        line = &g_pager->visible_buffer.rows[i];
        if (line_index < g_pager->buffer.line_count)
        {
            *line = g_pager->buffer.lines[line_index];
        }
        else
        {
            // Past end of text buffer
            *line =
                (struct pager_buffer_line) { NULL, 0, 0 };
        }

        // Draw the line
        moved = false;
        if (i < g_pager->buffer.line_count - g_pager->scroll)
        {
            // Whether line will be redrawn.  For full redraws this is always
            // the case, for partial redraw then we only re-print the selected
            // and deselected links.
            will_print = full;

        #define DRAW_START() \
            /* Move cursor to end of left margin, and fill the indent */ \
            if (!moved) \
            { \
                tui_cursor_move( \
                    g_pager->margin.l, \
                    i + 1 + PAGER_TOP); \
                tui_printf("%*s", line->indent, ""); \
                moved = true; \
            }

            const char *prefix_fmt = NULL, *addr_fmt = NULL;

            // Link highlighting
            if (line->link_index > -1 &&
                line->link_index < g_pager->link_count &&
                (full ||
                // If we are doing a partial refresh (just links) then we need
                // to only update the newly-selected link and the previously
                // deselected one.
                (!full && update_sel_link &&
                    (g_pager->link_index == line->link_index ||
                    g_pager->link_index_prev == line->link_index))))
            {
                DRAW_START();
                will_print = true;

                // Print the colours
                if (line->link_index == g_pager->link_index)
                {
                    prefix_fmt = COLOUR_PAGER_LINK_PROTOCOL_SELECTED;
                    addr_fmt = COLOUR_PAGER_LINK_LOCATION_SELECTED;
                    //tui_say(COLOUR_PAGER_LINK_SELECTED);
                }
                else
                {
                    prefix_fmt = COLOUR_PAGER_LINK_PROTOCOL;
                    addr_fmt = COLOUR_PAGER_LINK_LOCATION;
                    //tui_say(COLOUR_PAGER_LINK);
                }
            }

            // Don't print anything
            if (!will_print) continue;

            // Move cursor to end of left margin, and fill the indent
            DRAW_START();

        #if 0
            tui_sayn(line->s, line->bytes);
        #else
            if (prefix_fmt && addr_fmt)
            {
                tui_say(prefix_fmt);

                tui_sayn(line->s, line->proto_len);

                tui_say(addr_fmt);

                tui_sayn(line->s + line->proto_len,
                         line->bytes - line->proto_len);
            }
            else
                tui_sayn(line->s, line->bytes);
        #endif

            // Always clear the escapes after the line
            tui_say("\x1b[0m");

            // Don't clear if not full
            if (!full) continue;

        #undef DRAW_START
        }
        else
        {
            if (!full) continue;

            // Move cursor to end of left margin
            tui_cursor_move(g_pager->margin.l, i + 1 + PAGER_TOP);

        #if CLEAR_VI_STYLE
            // Because vi
            line->bytes = strlen(VI_EMPTY_CHAR_STR);
            line->len = VI_EMPTY_CHAR_STR_LEN;
            tui_sayn(VI_EMPTY_CHAR_STR, line->bytes);
        #else
            line->bytes = 0;
            line->len = 0;
        #endif
        }

        // Clear the old line
        int clear_count = max(
            ((int)g_pager->visible_buffer_prev.rows[i].len +
                g_pager->visible_buffer_prev.rows[i].indent) -
            ((int)line->len + line->indent), 0) + 1;
        tui_printf("%*s", clear_count, "");
    }

    // Highlight search matches
    search_highlight_matches();

    // Swap pointers to store old rows so we know what to clear on next paint
    struct pager_buffer_line *tmp = g_pager->visible_buffer.rows;
    g_pager->visible_buffer.rows = g_pager->visible_buffer_prev.rows;
    g_pager->visible_buffer_prev.rows = tmp;
    g_pager->link_index_prev = g_pager->link_index;
}

static void
pager_alloc_visible_buffer(struct pager_visible_buffer *b, int h)
{
    size_t next_size = g_tui->h * sizeof(struct pager_buffer_line);
    if (b->rows != NULL)
    {
        if (b->rows_size >= next_size)
        {
            // No need to reallocate; we already have enough space
            b->w = g_tui->w;
            b->h = h;
            memset(b->rows, 0, h * sizeof(struct pager_buffer_line));
            return;
        }

        // Need reallocate
        free(b->rows);
        b->rows = NULL;
    }

    b->w = g_tui->w;
    b->h = h;

    // Allocate a bit more to prevent reallocating every time the bloody window
    // expands
    b->rows_size = (next_size * 3) / 2;
    b->rows = malloc(b->rows_size);
    memset(b->rows, 0, next_size);
    if (!b->rows)
    {
        fprintf(stderr, "out of memory\n");
        exit(-1);
    }
}

void
pager_select_first_link_visible(void)
{
    for (int i = 0; i < g_pager->link_count; ++i)
    {
        // Skip lines that are before where the visible buffer starts
        if (g_pager->links[i].line_index < g_pager->scroll) continue;

        g_pager->link_index = i;
        return;
    }
}

void
pager_select_last_link_visible(void)
{
    if (g_pager->visible_buffer.h == 0) return;

    for (int i = g_pager->link_count - 1; i > -1; --i)
    {
        // Skip lines that are after where the visible buffer starts
        if (g_pager->links[i].line_index >=
            g_pager->scroll + g_pager->visible_buffer.h) continue;

        g_pager->link_index = i;
        return;
    }
}

/* Ensure a new link can be added without overflow */
void
pager_check_link_capacity(void)
{
    if (g_pager->link_count + 1 < g_pager->link_capacity) return;

    // Reallocate links
    size_t new_cap = (g_pager->link_capacity * 3) / 2;
    void *tmp = realloc(
        g_pager->links,
        new_cap * sizeof(struct pager_link));
    if (!tmp)
    {
        // Failed to allocate
        fprintf(stderr, "out of memory\n");
        exit(-1);
    }
    g_pager->link_capacity = new_cap;
    g_pager->links = tmp;
}
