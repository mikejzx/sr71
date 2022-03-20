#include "pch.h"
#include "pager.h"
#include "state.h"
#include "typesetter.h"

struct pager_state *g_pager;

static void pager_alloc_visible_buffer(struct pager_visible_buffer *, int);

void
pager_init(void)
{
    int i;
    g_pager = &g_state->pager;
    memset(g_pager, 0, sizeof(struct pager_state));

    g_pager->visible_buffer.rows = NULL;
    g_pager->visible_buffer_prev.rows = NULL;

    typesetter_init(&g_pager->typeset);
}

void
pager_update_page(const char *content, size_t content_size)
{
    g_pager->link_count = 0;
    g_pager->selected_link_index = -1;
    g_pager->scroll = 0;

    // Copy page data into the typesetter
    typesetter_reinit(&g_pager->typeset, content, content_size);

    // Typeset the content
    typeset_gemtext(&g_pager->typeset,
        &g_pager->buffer,
        min(g_pager->visible_buffer.w, 70));

    // Just refresh the whole TUI
    tui_repaint();
}

void
pager_deinit(void)
{
    if (g_pager->visible_buffer.rows)
    {
        free(g_pager->visible_buffer.rows);
    }
    if (g_pager->visible_buffer_prev.rows)
    {
        free(g_pager->visible_buffer_prev.rows);
    }
    free(g_pager->buffer.b);
    free(g_pager->buffer.lines);
    typesetter_deinit(&g_pager->typeset);
}

void
pager_resized(void)
{
    // Pager takes up whole screen except bottom two rows (for status line)
    int pager_height = max(g_tui->h - 2, 1);

    // Allocate visible buffers (if not already allocated)
    pager_alloc_visible_buffer(&g_pager->visible_buffer, pager_height);
    pager_alloc_visible_buffer(&g_pager->visible_buffer_prev, pager_height);

    // For restoring scroll position after the resize.
    int scroll_raw_index = 0;
    size_t scroll_raw_dist = 0;
    if (g_pager->buffer.line_count)
    {
        scroll_raw_index = g_pager->buffer.lines[g_pager->scroll].raw_index;
        scroll_raw_dist = g_pager->buffer.lines[g_pager->scroll].raw_dist;
    }

    // Re-typeset the content, to update word-wrapping, etc.
    typeset_gemtext(&g_pager->typeset,
        &g_pager->buffer,
        min(g_pager->visible_buffer.w, 70));

    // Now we need to restore the scroll position by finding the line that has
    // the raw index we had
    for (int r = 0; r < g_pager->buffer.line_count; ++r)
    {
        if (g_pager->buffer.lines[r].raw_index != scroll_raw_index) continue;

        // Store the top of the wrapped line initially
        g_pager->scroll = r;

        // Call it a day if there was no previous buffer
        if (g_pager->visible_buffer_prev.h == 0) break;

        // The text we are trying to find
        const char *old_buffer_text = g_pager->visible_buffer_prev.rows[0].s;
        size_t old_buffer_text_len = g_pager->visible_buffer_prev.rows[0].bytes;

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

void
pager_scroll_top(void) { g_pager->scroll = 0; }

void
pager_scroll_bottom(void)
{
    g_pager->scroll = g_pager->buffer.line_count - g_pager->visible_buffer.h;
}

/* Re-paint the entire pager */
void
pager_paint(void)
{
    // Draw the visible buffer
    for (int i = 0; i < g_pager->visible_buffer.h; ++i)
    {
        int line_index = i + g_pager->scroll;
        if (line_index < g_pager->buffer.line_count)
        {
            g_pager->visible_buffer.rows[i] = g_pager->buffer.lines[line_index];
        }
        else
        {
            // Past end of text buffer
            g_pager->visible_buffer.rows[i] =
                (struct pager_buffer_line) { NULL, 0, 0 };
        }

        // Move cursor to correct place
        tui_cursor_move(0, i + 1);

        struct pager_buffer_line *const line = &g_pager->visible_buffer.rows[i];

        // Draw the line
        if (i < g_pager->buffer.line_count - g_pager->scroll)
        {
            size_t len = line->len;
            if (len < g_pager->visible_buffer.w)
            {
                len = line->bytes;
            }
            else
            {
                len = g_pager->visible_buffer.w;
            }

            // Line highlighting
            bool highlighted = false;
            for (int l = 0; l < g_pager->link_count; ++l)
            {
                if (g_pager->links[l].buffer_loc == line->s)
                {
                    // TODO: check that these escape codes are proper
                    if (l == g_pager->selected_link_index)
                    {
                        tui_say("\x1b[35m");
                    }
                    else
                    {
                        tui_say("\x1b[34m");
                    }
                    highlighted = true;
                    break;
                }
            }

            tui_sayn(line->s, len);

            if (highlighted)
            {
                tui_say("\x1b[0m");
            }
        }
        else
        {
            // Because vi
            tui_say("~");
            line->bytes = 1;
            line->len = 1;
        }

        // Clear out the old line part that was here
        //int clear_count = min(g_pager->visible_buffer_prev.rows[i].bytes,
        //    g_pager->visible_buffer.w) - line->len;
        int clear_count =
            g_pager->visible_buffer_prev.rows[i].bytes - line->len;
        tui_printf("%*s", clear_count, "");
    }

    // Swap pointers to store old rows so we know what to clear on next paint
    struct pager_buffer_line *tmp = g_pager->visible_buffer.rows;
    g_pager->visible_buffer.rows = g_pager->visible_buffer_prev.rows;
    g_pager->visible_buffer_prev.rows = tmp;
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

        g_pager->selected_link_index = i;
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
        if (g_pager->links[i].line_index >
            g_pager->scroll + g_pager->visible_buffer.h) continue;

        g_pager->selected_link_index = i;
        return;
    }
}
