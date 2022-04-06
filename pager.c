#include "pch.h"
#include "pager.h"
#include "state.h"
#include "typesetter.h"

static const int CONTENT_WIDTH_PREFERRED = 70;

struct pager_state *g_pager = NULL;

static void pager_alloc_visible_buffer(struct pager_visible_buffer *, int);

static void
pager_recalc_margin(void)
{
    int marg = g_pager->visible_buffer.w - CONTENT_WIDTH_PREFERRED;
    g_pager->margin_bias = 0.3f;
    g_pager->margin.l = max(0, marg * g_pager->margin_bias);
    g_pager->margin.r = max(0, marg * (1.0f - g_pager->margin_bias));
}

void
pager_init(void)
{
    g_pager = &g_state->pager;
    memset(g_pager, 0, sizeof(struct pager_state));

    g_pager->visible_buffer.rows = NULL;
    g_pager->visible_buffer_prev.rows = NULL;

    g_pager->link_capacity = 10;
    g_pager->links = malloc(g_pager->link_capacity * sizeof(struct pager_link));

    g_pager->search.match_count = 0;
    g_pager->search.match_capacity = 1024;
    g_pager->search.matches =
        malloc(sizeof(struct search_match) * g_pager->search.match_capacity);

    typesetter_init(&g_pager->typeset);

    g_pager->search.invalidated = true;
}

void
pager_update_page(int selected, int scroll)
{
    g_pager->link_count = 0;
    g_pager->selected_link_index = selected;
    g_pager->scroll = scroll;

    // Reset marks
    memset(g_pager->marks, 0, sizeof(g_pager->marks));

    // Reset search
    g_pager->search.invalidated = true;
    g_pager->search.match_count = 0;

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
    free(g_pager->search.matches);
    free(g_pager->buffer.b);
    free(g_pager->buffer.lines);
    typesetter_deinit(&g_pager->typeset);
}

void
pager_resized(void)
{
    // Pager takes up whole screen except bottom two rows (for status line)
    int pager_height = max(g_tui->h - 2, 1);

    bool width_changed = g_pager->visible_buffer.w != g_tui->w;

    // Allocate visible buffers (if not already allocated)
    pager_alloc_visible_buffer(&g_pager->visible_buffer, pager_height);
    pager_alloc_visible_buffer(&g_pager->visible_buffer_prev, pager_height);

    // Don't need to do anything else unless width had changed (as width
    // affects word-wrapping, etc.)
    if (!width_changed) return;

    // We need to update search match positions
    g_pager->search.invalidated = true;
    tui_search_refresh();

    // For restoring scroll position after the resize.
    int scroll_raw_index = 0;
    size_t scroll_raw_dist = 0;
    if (g_pager->buffer.line_count)
    {
        scroll_raw_index = g_pager->buffer.lines[g_pager->scroll].raw_index;
        scroll_raw_dist = g_pager->buffer.lines[g_pager->scroll].raw_dist;
    }

    pager_recalc_margin();

    // Re-typeset the content, to update word-wrapping, etc.
    typeset_page(&g_pager->typeset,
        &g_pager->buffer,
        g_pager->visible_buffer.w - g_pager->margin.l - g_pager->margin.r,
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
    bool updating_links =
        g_pager->selected_link_index_prev != g_pager->selected_link_index;

    // Partial updates only run if selected link changed
    if (!full && !updating_links) return;

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

        struct pager_buffer_line *const line = &g_pager->visible_buffer.rows[i];

        // Draw the line
        bool highlighted = false;
        bool moved = false;
        bool will_print;
        if (i < g_pager->buffer.line_count - g_pager->scroll)
        {
            will_print = full;

            // Line highlighting
            for (int l = 0; l < g_pager->link_count; ++l)
            {
                // If we are doing a partial refresh, of just links, then we
                // need to only update the link that was selected and the one
                // that was deselected
                if (!full &&
                    updating_links &&
                    l != g_pager->selected_link_index &&
                    l != g_pager->selected_link_index_prev)
                {
                    continue;
                }

                struct pager_link *link = &g_pager->links[l];
                if (line->s >= link->buffer_loc &&
                    line->s < link->buffer_loc + link->buffer_loc_len)
                {
                    // Move cursor to correct place and fill margin and indent
                    if (!moved)
                    {
                        tui_cursor_move(0, i + 1);
                        tui_printf("%*s", g_pager->margin.l + line->indent, "");
                        moved = true;
                    }
                    will_print = true;

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

            // Don't print anything
            if (!will_print) continue;

            // Move cursor to correct place and fill margin
            if (!moved)
            {
                tui_cursor_move(0, i + 1);
                tui_printf("%*s", g_pager->margin.l + line->indent, "");
                moved = true;
            }

            line->len = min(line->len,
                g_pager->visible_buffer.w -
                g_pager->margin.l /*-
                g_pager->margin.r*/);
            line->bytes = utf8_size_w_formats(line->s, line->len);

            tui_sayn(line->s, line->bytes);

            //if (highlighted)
            {
                // Always clear the escapes after the line
                tui_say("\x1b[0m");
            }

            // Don't clear if not full
            if (!full) continue;
        }
        else
        {
            if (!full) continue;

            // Move cursor to correct place and fill margin
            tui_cursor_move(0, i + 1);
            tui_printf("%*s", g_pager->margin.l, "");

        #define CLEAR_VI_STYLE
        #ifdef CLEAR_VI_STYLE
            // Because vi
            static const char *EMPTY_CHAR = "\x1b[2m~\x1b[0m";
            line->bytes = strlen(EMPTY_CHAR);
            line->len = utf8_strnlen_w_formats(EMPTY_CHAR, line->bytes);
            tui_sayn(EMPTY_CHAR, line->bytes);
        #else
            line->bytes = 0;
            line->len = 0;
        #endif
        }

        // Clear old line
        tui_cursor_move(
            (int)line->len + g_pager->margin.l + 1 + line->indent, i + 1);
        int clear_count =
            (int)g_pager->visible_buffer_prev.rows[i].len - (int)line->len +
            max(g_pager->visible_buffer_prev.rows[i].indent - line->indent, 0);
        clear_count = max(clear_count, 0);
        //clear_count = min(max(clear_count, 0),
        //    g_pager->visible_buffer.w - (int)line->len);
        tui_printf("%*s", clear_count, "");
    }

    // Highlight search matches
    // This needs some improvement; still causes glitched-out rendering at the
    // moment...
    const struct search_match *match;
    for (int i = 0; i < g_pager->search.match_count; ++i)
    {
        match = &g_pager->search.matches[i];

        if (match->begin.line < g_pager->scroll ||
            match->begin.line >= g_pager->scroll + g_pager->visible_buffer.h)
        {
            continue;
        }

        // Match occurs in visible buffer; so move cursor to it's begin/end
        // range and write some nice escape codes

        const struct pager_buffer_line
            *const line_begin = &g_pager->buffer.lines[match->begin.line],
            *line_end = &g_pager->buffer.lines[match->end.line],
            *line_last_visible;

        // Make sure ending line is within the visible buffer
        line_last_visible = &g_pager->buffer.lines[max(min(
            g_pager->scroll + g_pager->visible_buffer.h - 1,
            g_pager->buffer.line_count - 1), 0)];
        for (;
            line_end > line_last_visible;
            --line_end);

        tui_cursor_move(
            g_pager->margin.l + 1 + line_begin->indent +
            utf8_strnlen_w_formats(
                line_begin->s, match->begin.loc - line_begin->s),
            (match->begin.line - g_pager->scroll) + 1);
        tui_say("\x1b[7m");

        if (line_end != line_begin) // spans multiple lines
        {
            // Print out the highlighted word to end of beginning line
            tui_sayn(match->begin.loc,
                line_begin->s + line_begin->bytes - match->begin.loc);

            // And print out lines in between
            for (const struct pager_buffer_line *line_cur = line_begin + 1;
                line_cur < line_end;
                ++line_cur)
            {
                tui_cursor_move(
                    g_pager->margin.l + 1 + line_cur->indent,
                    (line_cur - line_begin +
                     match->begin.line - g_pager->scroll) + 1);
                tui_sayn(line_cur->s, line_cur->bytes);
            }

            // And the rest of the highlighted word on the ending line
            tui_cursor_move(
                g_pager->margin.l + 1 + line_end->indent,
                (match->end.line - g_pager->scroll) + 1);
            tui_sayn(line_end->s, match->end.loc - line_end->s);
        }
        else // single line
        {
            // Print out the highlighted word to end of range
            tui_sayn(match->begin.loc, match->end.loc - match->begin.loc);
        }

        tui_say("\x1b[27m");
    }

    // Swap pointers to store old rows so we know what to clear on next paint
    struct pager_buffer_line *tmp = g_pager->visible_buffer.rows;
    g_pager->visible_buffer.rows = g_pager->visible_buffer_prev.rows;
    g_pager->visible_buffer_prev.rows = tmp;
    g_pager->selected_link_index_prev = g_pager->selected_link_index;
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
        if (g_pager->links[i].line_index >=
            g_pager->scroll + g_pager->visible_buffer.h) continue;

        g_pager->selected_link_index = i;
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

/* Find next occurrance of search query from scroll point */
void
pager_search_next(void)
{
    struct search *const s = &g_pager->search;

    if (s->invalidated) tui_search_refresh();

    if (s->match_count == 0)
    {
        tui_status_say("Pattern not found");
        return;
    }

    // Wrap search
    int search_pos = (s->index == s->match_count - 1) ? 0 : g_pager->scroll;

    for (int i = 0; i < s->match_count; ++i)
    {
        if (s->matches[i].begin.line >= search_pos)
        {
            if (s->index == i)
            {
                // Check if there's any other matches on this line we could use
                // instead
                for (int j = i + 1; j < s->match_count; ++j)
                {
                    if (s->matches[i].begin.line == search_pos)
                    {
                        i = j;
                        break;
                    }
                }
            }

            s->index = i;
            g_pager->scroll = s->matches[i].begin.line;
            break;
        }
    }

    tui_invalidate(INVALIDATE_PAGER_BIT);

    tui_status_begin();
    tui_printf("%c%s %u/%u",
        s->reverse ? '?' : '/',
        s->query,
        s->index + 1, s->match_count);
    tui_status_end();
}

/* Find previous occurrance of search query from scroll point */
void
pager_search_prev(void)
{
    struct search *const s = &g_pager->search;

    if (s->invalidated) tui_search_refresh();

    if (s->match_count == 0)
    {
        tui_status_say("Pattern not found");
        return;
    }

    // Wrap the search
    int search_pos = (s->index == 0) ?
        (g_pager->buffer.line_count - 1) : g_pager->scroll;

    for (int i = s->match_count - 1; i > -1; --i)
    {
        if (s->matches[i].begin.line <= search_pos)
        {
            if (s->index == i)
            {
                // Check if there's any other matches on this line we could use
                // instead
                for (int j = i - 1; j > -1; --j)
                {
                    if (s->matches[i].begin.line == search_pos)
                    {
                        i = j;
                        break;
                    }
                }
            }

            s->index = i;
            g_pager->scroll = s->matches[i].begin.line;
            break;
        }
    }

    tui_invalidate(INVALIDATE_PAGER_BIT);

    tui_status_begin();
    tui_printf("%c%s %u/%u",
        s->reverse ? '?' : '/',
        s->query,
        s->index + 1, s->match_count);
    tui_status_end();
}

/*
 * Search function that works across wrapped lines of the buffer, including
 * across hyphenations
 */
int
pager_multiline_search(
    // String/location that we are currently comparing
    const char *restrict s1,
    // Search query to check for match from s1
    const char *restrict query,
    // The pager buffer and index of line that s1 begins on
    struct pager_buffer *restrict b,
    int line_index,
    struct search_match *match)
{
    /*
     * There's a few rules we lay out to determine if a match occurs
     * + Whitespace counts are always ignored; i.e. spaces will always match if
     *   there is at least one whitespace character; this helps simplify the
     *   wrapped word search a bit.
     * + If the comparison has been good so far and we reach the end of the
     *   line; there is 3 possible outcomes to consider:
     *     0.) the match succeeds because there is no characters left of the
     *         query.
     *
     *         query="foo"
     *
     *         |This is a line foo| <-- match succeeds
     *         |A second line     |
     *
     *     1.) the current word being checked does not have whitespace, and is
     *         hyphenated across the line, so the check continues on the next
     *         line
     *
     *         query="foobar"
     *
     *                     |This is a line foo-|
     *         move here ->|bar second line    |
     *
     *     2.) the current word being checked in the query has whitespace where
     *         we are and we are at end of the current line, so match continues
     *         from next non-whitespace character (usually first char) on the
     *         next line
     *
     *         query="foo bar"
     *
     *                     |This is a line foo|
     *                     |bar second line   |
     *                      ^-- begin here, technically should not be any
     *                          actual whitespace before this on this line
     *                          because of the line breaking algorithm.
     *
     * + In future, we will also need to make considerations for other things,
     *   like ignoring escape codes (in case of bold/italic highlighting) and
     *   could provide matches for e.g. UTF-8 bullet point character to
     *   asterisks, etc.
     * + Matching of multi-byte UTF-8 sequences hasn't been tested, but
     *   it should work reasonably fine, I think.
     */

    // Current line we are on and searching
    const char *line = b->lines[line_index].s,
               *line_end = line + b->lines[line_index].bytes;

    // Match until we reach end of query
    const char *c;
    for (c = s1;;)
    {
        // Check that current character matches
        if (!*query) goto match;

        //if (*c != *query) break;
        if (tolower(*c) != tolower(*query)) break;

        // Move to next char
        c = next_char_w_formats(c, line_end);
        ++query;

        // Check for match
        if (!*query) goto match;

        if (*query == ' ')
        {
            // Skip any more white space
            for (; *query && *query == ' '; ++query);

            // Move to next line
            if (!*c || c >= line_end) goto next_line;

            // Skip white space in pointer too
            for (;
                c < line_end && *c && *c == ' ';
                c = next_char_w_formats(c, line_end));

            continue;
        }

        // If we reach a hyphen at the end of the line, then move to the next
        // line
        if (c >= line_end - 1 &&
            *c == '-' &&
            b->lines[line_index].is_hyphenated)
        {
            // Move to next line
            goto next_line;
        }

        continue;
    next_line:
        ++line_index;
        if (line_index >= b->line_count) break;
        line     = b->lines[line_index].s;
        line_end = line + b->lines[line_index].bytes;
        for (c = line;
            c < line_end && *c && *c == ' ';
            c = next_char_w_formats(c, line_end));
    }

    return -1;

match:
    match->end.line = line_index;
    match->end.loc = c;
    return 0;
}
