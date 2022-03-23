#include "pch.h"
#include "state.h"
#include "gemini.h"
#include "typesetter.h"
#include "pager.h"
#include "tui.h"

void
typesetter_init(struct typesetter *t)
{
    memset(t, 0, sizeof(struct typesetter));
}

/* Initialise typesetter with raw content data */
void
typesetter_reinit(struct typesetter *t)
{
    // Count lines
    size_t old_line_count = t->raw_line_count;
    t->raw_line_count = 0;
    for (int i = 0; i < g_recv->size; ++i)
    {
        if (g_recv->b[i] == '\n' || i == g_recv->size - 1) ++t->raw_line_count;
    }

    // Allocate for lines
    const size_t lines_size =
        sizeof(struct pager_buffer_line) * t->raw_line_count;
    if (!t->raw_lines)
    {
        t->raw_lines = malloc(lines_size);
    }
    else if (old_line_count < t->raw_line_count)
    {
        free(t->raw_lines);
        t->raw_lines = malloc(lines_size);
    }

    // Set line points in raw buffer
    int l = 0, l_prev = -1;
    for (int i = 0; i < g_recv->size; ++i)
    {
        if (g_recv->b[i] != '\n' && i != g_recv->size - 1) continue;

        t->raw_lines[l].s = &g_recv->b[l_prev + 1];
        t->raw_lines[l].bytes = i - l_prev - 1;
        t->raw_lines[l].len = utf8_strnlen_w_formats(
            &g_recv->b[l_prev + 1],
            t->raw_lines[l].bytes);

        l_prev = i;
        ++l;
    }
}

void
typesetter_deinit(struct typesetter *t)
{
    if (t->raw_lines) free(t->raw_lines);
}

/* Typeset gemtext to a pager buffer */
void
typeset_gemtext(
    struct typesetter *t,
    struct pager_buffer *b,
    size_t width_total)
{
    struct margin
    {
        int l, r;
    } margin;
    margin.l = 4;
    margin.r = 8;

    width_total -= margin.l;
    width_total -= margin.r;

    // Avoid errors and memory issues by just not rendering at all if the width
    // is stupidly small
    if (width_total < 15) return;

    // Allocate space for buffer if needed
    if (!b->b)
    {
        b->b = malloc(g_recv->size);
    }
    else if (b->size < g_recv->size)
    {
        b->size = g_recv->size;
        if (b->b) free(b->b);
        b->b = malloc(b->size);
    }
    if (!b->b) return;

    // Allocate space for lines.  The number we compute assumes that EVERY line
    // will be wrapped to the width (even though they aren't) and *should*
    // guarantee a large enough buffer
    size_t wanted_line_count = 0;
    for (int raw_index = 0; raw_index < t->raw_line_count; ++raw_index)
    {
        wanted_line_count += 1 +
            ceil(t->raw_lines[raw_index].bytes / (float)width_total);
    }
    const size_t wanted_lines_size =
        wanted_line_count * sizeof(struct pager_buffer_line);
    if (!wanted_lines_size) return;
    if (!b->lines)
    {
        b->lines = malloc(wanted_lines_size);
        b->lines_capacity = wanted_lines_size;
    }
    else if (b->lines_capacity < wanted_lines_size)
    {
        if (b->lines) free(b->lines);
        b->lines = malloc(wanted_lines_size);
        b->lines_capacity = wanted_lines_size;
    }
    if (!b->lines) return;

    // Reset buffer state before we re-write it
    b->line_count = 0;
    g_pager->link_count = 0;

    // Position in buffer that we're up to
    char *buffer_pos = b->b;

    // Current gemtext parsing state
    struct gemtext_state
    {
        bool is_verbatim;
        bool is_list;
        int indent;

        // Distance that our current line is from the beginning of it's raw
        // line (in case of wrapping)
        size_t raw_dist;

        // Number of bytes to skip over in the raw string (e.g. to skip over
        // asterisks in list)
        size_t raw_bytes_skip;

        // Escape formatting code needs to be cleared before the next line
        bool need_clear_esc;
    } gemtext;
    memset(&gemtext, 0, sizeof(struct gemtext_state));

    // The following macros operate on the current line (by appending to them).
    // They are here in hopes to massively reduce duplication of some hideous
    // code and hence hopefully make the rendering code a bit cleaner
#define LINE_START() \
    do \
    { \
        if (line_started) break; \
        line_started = true; \
        line->s = buffer_pos; \
        line->bytes = 0; \
        line->raw_index = raw_index; \
        line->raw_dist = 0; \
    } while(0)
#define LINE_FINISH() \
    do \
    { \
        if (!line_started) break; \
        line_started = false; \
        line->len = utf8_strnlen_w_formats(line->s, line->bytes); \
        line->raw_dist = gemtext.raw_dist; \
        if ((b->line_count + 1) * sizeof(struct pager_buffer_line) >= \
            b->lines_capacity) \
        { \
            /* just abort if line count gets stupid (due to tiny widths */ \
            return; \
        } \
        ++b->line_count; \
        ++line; \
    } while(0)
#define LINE_INDENT() \
    LINE_PRINTF("%*s", gemtext.indent, "");
#define LINE_STRNCPY(str, len) \
    do \
    { \
        const size_t n_bytes = (len); \
        if (buffer_pos + n_bytes >= buffer_end_pos) break; \
        strncpy(buffer_pos, \
            (str), \
            min(n_bytes, buffer_end_pos - buffer_pos)); \
        line->bytes += n_bytes; \
        buffer_pos += n_bytes; \
        gemtext.raw_dist += n_bytes; \
    } while(0)
#define LINE_STRNCPY_LIT(str) LINE_STRNCPY((str), strlen((str)))
#define LINE_PRINTF(fmt, ...) \
    const size_t LINE_PRINTF_N_BYTES = \
        snprintf(buffer_pos, \
            buffer_end_pos - buffer_pos, \
            (fmt), \
            __VA_ARGS__); \
    line->bytes += LINE_PRINTF_N_BYTES; \
    buffer_pos += LINE_PRINTF_N_BYTES; \
    gemtext.raw_dist += LINE_PRINTF_N_BYTES;

    // Iterate over each of the raw lines in the buffer
    struct pager_buffer_line *line = b->lines;
    const char *const buffer_end_pos = b->b + b->size;
    bool line_started = false;
    for (int raw_index = 0;
        raw_index < t->raw_line_count;
        ++raw_index)
    {
        // The current raw line that we are on
        const struct pager_buffer_line *const
            rawline = &t->raw_lines[raw_index];

        // Reset raw distance for sub-lines on this line.
        gemtext.raw_dist = 0;

        // Start a new line
        LINE_START();

        // Check for preformatting
        if (rawline->bytes > strlen("```") &&
            strncmp(rawline->s, "```", strlen("```")) == 0)
        {
            // Empty line
            LINE_FINISH();
            gemtext.is_verbatim ^= true;
            continue;
        }

        if (gemtext.is_verbatim)
        {
            // Print text verbatim
            gemtext.indent = 1;
            LINE_INDENT();
            LINE_STRNCPY(rawline->s, rawline->bytes);
            LINE_FINISH();
            gemtext.indent = 0;
            continue;
        }

        // Check for headings
        int heading_level = 0;
        for (const char *x = rawline->s;
            x < (rawline->s + rawline->bytes) && *x == '#';
            ++heading_level, ++x);
        switch(heading_level)
        {
        case 1:
            LINE_STRNCPY_LIT("\x1b[1;36m");
            gemtext.raw_bytes_skip = 2;
            gemtext.need_clear_esc = true;
            break;

        case 2:
            LINE_STRNCPY_LIT("\x1b[36m");
            gemtext.raw_bytes_skip = 3;
            gemtext.need_clear_esc = true;
            break;

        case 3:
            LINE_STRNCPY_LIT("\x1b[1m");
            gemtext.raw_bytes_skip = 4;
            gemtext.need_clear_esc = true;
            break;

        default: break;
        }

        // Parse links
        if (rawline->bytes > strlen("=>") &&
            strncmp(rawline->s, "=>", strlen("=>")) == 0)
        {
            pager_check_link_capacity();
            size_t l_index = g_pager->link_count++;

            /* Get URI. */
            // Begins where there is no whitespace, and cannot begin with an
            // '=' or '>'.  Ends at whitespace
            const char *l_uri =
                rawline->s + strspn(rawline->s, "=> \t");
            size_t l_uri_len = min(strcspn(l_uri, "\t \n"), rawline->bytes);

            /* Get optional human-friendly label. */
            // Begins at first non-whitespace character after URI
            const char *l_title;
            size_t l_title_len;
            if (strcspn(l_uri, "\t ") < rawline->bytes)
            {
                l_title = l_uri + l_uri_len;
                l_title += strspn(l_title, "\t ");
                l_title_len = rawline->s + rawline->bytes - l_title;
            }
            else
            {
                l_title = l_uri;
                l_title_len = l_uri_len;
            }

            // Add link to page state and set buffer location so we can
            // highlight on selection
            struct pager_link *l = &g_pager->links[l_index];
            l->uri = uri_parse(l_uri, l_uri_len);
            uri_abs(&g_state->uri, &l->uri);
            l->buffer_loc = buffer_pos;
            l->line_index = b->line_count;
            // ... note len is set below printing string

            // Create the string
            LINE_PRINTF(" [%d] %.*s",
                (int)l_index,
                (int)l_title_len,
                l_title);

            // A bit dodgey but it works
            l->buffer_loc_len = LINE_PRINTF_N_BYTES;

            goto finish;
        }

        // Parse lists
        if (rawline->bytes > strlen("* ") &&
            strncmp(rawline->s, "* ", strlen("* ")) == 0)
        {
            gemtext.indent = 1;
            gemtext.is_list = true;
        }
        else
        {
            gemtext.is_list = false;
            gemtext.indent = 0;
        }

        // Apply indent
        LINE_INDENT();

        if (gemtext.is_list)
        {
            // Apply our own bullet char to lists
            LINE_STRNCPY_LIT("\u2022");

            // Wrapped list lines get nicer indentation
            gemtext.indent = 3;

            // Skip over the asterisk char
            gemtext.raw_bytes_skip = 1;
        }

        // Margins are accounted for in "width total" here
        int width = width_total - gemtext.indent;

        // New line wrap method; iterate over each word in the line one-by-one,
        // and add them to the buffer.  This fixes the annoying infinite-loop
        // when a large word would span across lines.
        const char *c_prev = rawline->s + gemtext.raw_bytes_skip;
        gemtext.raw_bytes_skip = 0;
        int chars_this_column = 0;
        for (const char *c = c_prev;
            c <= rawline->s + rawline->bytes;
            ++c)
        {
            // Found a space or we are at end of line
            if (*c == ' ' ||
                c == rawline->s + rawline->bytes)
            {
                while (chars_this_column + c - c_prev > width)
                {
                    // Fill in what we can of this column with the beginning of
                    // this huge word
                    int chars_count = max(width - chars_this_column - 1, 0);
                    if (chars_count > 8)
                    {
                        // For now only allow hyphenation on actual letters.
                        // Apparently real hyphenation should only occur
                        // specifically at the ends of syllables too or
                        // something (for maximum-autist typography), but this
                        // is good enough for now and I think it looks fine
                        // (maybe a future feature we can implement)
                        // We search for the earliest acceptable character to
                        // hyphenate on, and give up and just hyphenate at the
                        // bad char anyway if there is nothing acceptable (to
                        // avoid infinite loops)
                        int chars_count_old = chars_count;
                        for (
                            const char *prev_char = c_prev + chars_count - 1;
                            !((*prev_char >= 'a' && *prev_char <= 'z') ||
                                (*prev_char >= 'A' && *prev_char <= 'A'));
                            chars_count = max(chars_count - 1, 0),
                            prev_char = c_prev + chars_count - 1)
                        {
                            if (prev_char <= c_prev)
                            {
                                chars_count = chars_count_old;
                                break;
                            }
                        }

                        LINE_STRNCPY(c_prev, chars_count);

                        // Hyphenate the next section of the word
                        LINE_STRNCPY_LIT("-");

                        c_prev += chars_count;
                    }

                    LINE_FINISH();
                    chars_this_column = 0;
                    LINE_START();
                    LINE_INDENT();

                    // Remove trailing whitespace
                    for (; *c_prev == ' '; ++c_prev);
                }

                chars_this_column +=
                    max(utf8_strnlen_w_formats(c_prev, c - c_prev), 0);

                // Add word to the line
                if (c - c_prev > 0) LINE_STRNCPY(c_prev, c - c_prev);
                c_prev = c;
            }
        }

    finish:
        if (gemtext.need_clear_esc)
        {
            LINE_STRNCPY_LIT("\x1b[0m");
            gemtext.need_clear_esc = false;
        }

        LINE_FINISH();
    }

    // Replace all tabs with spaces, as we can't render them properly right now
    // (without really breaking the rendering code)
    for (char *c = b->b; c < buffer_pos; ++c)
    {
        if (*c == '\t') *c = ' ';
    }

    return;
#if 0
abort:
    // Out of buffer space! TODO avoid this somehow
    tui_cmd_status_prepare();
    tui_say("typeset failed: out of buffer space! (FIXME)");
#endif
}
