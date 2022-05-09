#include "pch.h"
#include "pager.h"
#include "search.h"
#include "tui.h"

static struct search *s_search = NULL;

static void search_scroll_to_next(void);
static void search_scroll_to_prev(void);
static int
multiline_search_query(
    const char *restrict,
    const char *restrict,
    struct pager_buffer *restrict,
    int,
    struct search_match *);

/* Initialise search stuff */
void
search_init(void)
{
    s_search = &g_pager->search;

    s_search->match_count = 0;
    s_search->match_capacity = 1024;
    s_search->matches =
        malloc(sizeof(struct search_match) * s_search->match_capacity);

    s_search->invalidated = true;
}

/* De-initialise search stuff */
void
search_deinit(void)
{
    free(s_search->matches);
}

/* Reset current search state */
void
search_reset(void)
{
    s_search->invalidated = true;
    s_search->match_count = 0;
}

/* Cause search to be refreshed */
void
search_update(void)
{
    s_search->invalidated = true;
    search_perform();
}

/* Navigate to next search item (based on direction) */
void
search_next(void)
{
    if (g_pager->search.reverse) search_scroll_to_prev();
    else search_scroll_to_next();
}

/* Navigate to next search item (based on direction) */
void
search_prev(void)
{
    if (g_pager->search.reverse) search_scroll_to_next();
    else search_scroll_to_prev();
}

/* Perform search, assumes that query has already been set */
void
search_perform(void)
{
    struct search *const s = s_search;

    if (s->query_len <= 0) return;

    s->match_count = 0;
    s->invalidated = false;

    struct search_match match;

    // Search for all matches in the buffer itself
    for (int i = 0; i < g_pager->buffer.line_count; ++i)
    {
        struct pager_buffer_line *line = &g_pager->buffer.lines[i];

        // Simple linear search for the string
        for (const char *c = line->s;
            c < line->s + line->bytes;
            ++c)
        {
            // Check for match using our special search function
            if (multiline_search_query(
                c, s->query, &g_pager->buffer, i, &match) != 0) continue;

            s->matches[s->match_count++] = (struct search_match)
            {
                .begin =
                {
                    .line = i,
                    .loc = c,
                },
                .end = match.end
            };
        }
    }

    s->index = -1;

    if (s->match_count == 0)
    {
        tui_status_say("Pattern not found");
        return;
    }
}

/* Highlight all text in the visible buffer that matches our search query */
void
search_highlight_matches(void)
{
    // Highlight search matches
    const struct search_match *match;
    for (int i = 0; i < s_search->match_count; ++i)
    {
        match = &s_search->matches[i];

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
            g_pager->margin.l + line_begin->indent +
            utf8_width(
                line_begin->s, match->begin.loc - line_begin->s),
            (match->begin.line - g_pager->scroll) + 1);

        if (line_end != line_begin) // spans multiple lines
        {
            // Print out the highlighted word to end of beginning line
            tui_say("\x1b[7m");
            tui_sayn(match->begin.loc,
                line_begin->s + line_begin->bytes - match->begin.loc);
            tui_say("\x1b[27m");

            // And print out lines in between
            for (const struct pager_buffer_line *line_cur = line_begin + 1;
                line_cur < line_end;
                ++line_cur)
            {
                tui_cursor_move(
                    g_pager->margin.l + line_cur->indent,
                    (line_cur - line_begin +
                     match->begin.line - g_pager->scroll) + 1);
                tui_say("\x1b[7m");
                tui_sayn(line_cur->s, line_cur->bytes);
                tui_say("\x1b[27m");
            }

            // And the rest of the highlighted word on the ending line
            tui_cursor_move(
                g_pager->margin.l + line_end->indent,
                (match->end.line - g_pager->scroll) + 1);
            tui_say("\x1b[7m");
            tui_sayn(line_end->s, match->end.loc - line_end->s);
            tui_say("\x1b[27m");
        }
        else // single line
        {
            // Print out the highlighted word to end of range
            tui_say("\x1b[7m");
            tui_sayn(match->begin.loc, match->end.loc - match->begin.loc);
            tui_say("\x1b[27m");
        }
    }
}

/* Find next occurrance of search query from scroll point */
static void
search_scroll_to_next(void)
{
    struct search *const s = s_search;

    if (s->invalidated) search_perform();

    if (s->match_count == 0)
    {
        tui_status_say("Pattern not found");
        return;
    }

    // Start search from scroll position
    const int search_pos = g_pager->scroll;
    bool got = false;
    for (int i = 0; i < s->match_count; ++i)
    {
        if (s->matches[i].begin.line >= search_pos)
        {
            if (s->index != -1 && i <= s->index)
            {
                // Check if there's any other matches on this line we could use
                // instead
                for (int j = i + 1; j < s->match_count; ++j)
                {
                    if (s->matches[j].begin.line == search_pos)
                    {
                        i = j;
                        break;
                    }
                }
                if (s->index == i) continue;
            }

            s->index = i;
            g_pager->scroll = s->matches[i].begin.line;
            got = true;
            break;
        }
    }
    if (!got)
    {
        // Wrap search
        s->index = 0;
        g_pager->scroll = s->matches[s->index].begin.line;
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
static void
search_scroll_to_prev(void)
{
    struct search *const s = s_search;

    if (s->invalidated) search_perform();

    if (s->match_count == 0)
    {
        tui_status_say("Pattern not found");
        return;
    }

    // Start search from scroll position
    const int search_pos = g_pager->scroll;

    bool got = false;
    for (int i = s->match_count - 1; i > -1; --i)
    {
        if (s->matches[i].begin.line <= search_pos)
        {
            if (s->index != -1 && i >= s->index)
            {
                // Check if there's any other matches on this line we could use
                // instead
                for (int j = i - 1; j > -1; --j)
                {
                    if (s->matches[j].begin.line == search_pos)
                    {
                        i = j;
                        break;
                    }
                }
                if (s->index == i) continue;
            }

            s->index = i;
            g_pager->scroll = s->matches[i].begin.line;
            got = true;
            break;
        }
    }
    if (!got)
    {
        // Wrap
        s->index = s->match_count - 1;
        g_pager->scroll = s->matches[s->index].begin.line;
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
static int
multiline_search_query(
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
    const char *line = b->lines[line_index].s + b->lines[line_index].prefix_len,
               *line_end = b->lines[line_index].s + b->lines[line_index].bytes;

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
        line     = b->lines[line_index].s + b->lines[line_index].prefix_len;
        line_end = b->lines[line_index].s + b->lines[line_index].bytes;
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
