#include "pch.h"
#include "gemini.h"
#include "mime.h"
#include "pager.h"
#include "state.h"
#include "tui.h"
#include "typesetter.h"

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
        t->raw_lines[l].bytes = i - l_prev - (int)(g_recv->b[i] == '\n');

        if (i > 0 && g_recv->b[i - 1] == '\r')
        {
            // Get rid of carriage-returns; this also fixes clearing with the
            // pager
            --t->raw_lines[l].bytes;
        }

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

/* Prepare buffers, etc. for typesetting */
static bool
typeset_start(
    struct typesetter *t,
    struct pager_buffer *b,
    size_t width_total)
{
    // Reset buffer state before we re-write it
    b->line_count = 0;

    // TODO: don't re-parse links on every update?
    g_pager->link_count = 0;

    // Avoid errors and memory issues by just not rendering at all if the width
    // is stupidly small
    if (width_total < 10) return false;

    // Allocate space for buffer if needed
    if (!b->b || b->size < g_recv->size)
    {
        b->size = (g_recv->size * 3) / 2;
        if (b->b) free(b->b);
        b->b = malloc(b->size);
    }
    if (!b->b)
    {
        fprintf(stderr, "out of memory\n");
        exit(-1);
    }

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
    if (!wanted_lines_size) return false;
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
    if (!b->lines) return false;

    return true;
}

/* Stuff run after typesetting document */
static void
typeset_finish(
    struct pager_buffer *b,
    size_t n_bytes)
{
    // Replace all tabs with spaces, as we can't render them properly right now
    // (without really breaking the rendering code)
    for (char *c = b->b; c < b->b + n_bytes; ++c)
    {
        if (*c == '\t') *c = ' ';
    }
}

/* Typeset gemtext to a pager buffer */
static size_t
typeset_gemtext(
    struct typesetter *t,
    struct pager_buffer *b,
    size_t width_total)
{
    // Position in buffer that we're up to
    char *buffer_pos = b->b;

    // Current gemtext parsing state
    struct gemtext_state
    {
        enum gemtext_parse_mode
        {
            // Only a few here; the rest of the modes can be handled without a
            // flag
            PARSE_PARAGRAPH = 0,
            PARSE_VERBATIM,
            PARSE_LIST,
            PARSE_BLOCKQUOTE,
        } mode;

        // Reference to the link being parsed (if any)
        struct pager_link *link;

        int link_maxidx_strlen;

        // Current indent level
        int indent;

        // This is extra indent, but doesn't apply to first line in wrapped
        // lines
        int hang;

        // Distance that our current line is from the beginning of it's raw
        // line (in case of wrapping)
        size_t raw_dist;

        // Number of bytes to skip over in the raw string (e.g. to skip over
        // asterisks in list)
        size_t raw_bytes_skip;

        // Pointer in string to the most recent escape code used.  This is used
        // to preserve escapes across wrapped lines when the escape is actually
        // out of the visible buffer.  Happens e.g in a wrapped heading when
        // the initial line is out of view
        const char *esc;
        size_t esc_len;

        // Escape formatting code needs to be cleared before the next line
        bool need_clear_esc;

        // Prefix applied to wrapped lines
        char prefix[8];
        size_t prefix_len;
    } gemtext;
    memset(&gemtext, 0, sizeof(struct gemtext_state));

    // The following macros operate on the current line (by appending to them).
    // They are here in hopes to massively reduce duplication of some hideous
    // code and hence hopefully make the rendering code a bit cleaner
#define BUFFER_CHECK_SIZE(x) \
    if (buffer_pos + (x) >= buffer_end_pos) \
    { \
        /*
         * We just abort rendering if we exceed the maximum buffer size, as
         * it's pretty tricky to perform the reallocation (as we use pointers
         * to parts of the buffer so much).  TODO investigate better solution
         * The overflow would only occur (in rare cases with absolutely
         * enormous files) if huge indent amounts or link/list prefixes, etc.
         * add up.
         */ \
        tui_status_begin(); \
        tui_printf("Aborting render (exceeded buffer size %.1f KiB).  FIXME", \
            b->size / 1024.0f); \
        tui_status_end(); \
        return 0; \
    }
#define LINE_START() \
    do \
    { \
        if (line_started) break; \
        line_started = true; \
        line->s = buffer_pos; \
        line->raw_index = raw_index; \
        line->raw_dist = 0; \
        line->is_heading = false; \
    } while(0)
#define LINE_FINISH() \
    do \
    { \
        if (!line_started) break; \
        line_started = false; \
        line->bytes = buffer_pos - line->s; \
        line->len = utf8_strnlen_w_formats(line->s, line->bytes); \
        line->raw_dist = gemtext.raw_dist; \
        if ((b->line_count + 1) * sizeof(struct pager_buffer_line) >= \
            b->lines_capacity) \
        { \
            /* just abort if line count gets stupid (due to tiny widths) */ \
            return 0; \
        } \
        ++b->line_count; \
        ++line; \
    } while(0)
#define LINE_INDENT(do_hang) \
    do \
    { \
        int indent_amount = gemtext.indent + \
            ((do_hang) ? gemtext.hang : 0); \
        BUFFER_CHECK_SIZE(indent_amount); \
        LINE_PRINTF("%*s", indent_amount, ""); \
    } while(0)
#define LINE_STRNCPY(str, len) \
    do \
    { \
        const size_t n_bytes = (len); \
        BUFFER_CHECK_SIZE(n_bytes); \
        strncpy(buffer_pos, \
            (str), \
            min(n_bytes, buffer_end_pos - buffer_pos)); \
        buffer_pos += n_bytes; \
        gemtext.raw_dist += n_bytes; \
        if (gemtext.link) gemtext.link->buffer_loc_len += n_bytes; \
    } while(0)
#define LINE_STRNCPY_LIT(str) LINE_STRNCPY((str), strlen((str)))
#define LINE_PRINTF(fmt, ...) \
    const size_t LINE_PRINTF_N_BYTES = \
        snprintf(buffer_pos, \
            buffer_end_pos - buffer_pos, \
            (fmt), \
            __VA_ARGS__); \
    BUFFER_CHECK_SIZE(LINE_PRINTF_N_BYTES); \
    buffer_pos += LINE_PRINTF_N_BYTES; \
    gemtext.raw_dist += LINE_PRINTF_N_BYTES; \
    if (gemtext.link) gemtext.link->buffer_loc_len += LINE_PRINTF_N_BYTES;

    int raw_index;

    // Count links in the page
    {
        int maxidx = 0;
        for (raw_index = 0;
            raw_index < t->raw_line_count;
            ++raw_index)
        {
            const struct pager_buffer_line *const
                l = &t->raw_lines[raw_index];
            if (l->bytes > strlen("=>") &&
                strncmp(l->s, "=>", strlen("=>")) == 0)
            {
                ++maxidx;
            }
        }
        gemtext.link_maxidx_strlen = snprintf(NULL, 0, "%d", maxidx);
    }

    // Iterate over each of the raw lines in the buffer
    struct pager_buffer_line *line = b->lines;
    const char *const buffer_end_pos = b->b + b->size;
    bool line_started = false;
    for (raw_index = 0;
        raw_index < t->raw_line_count;
        ++raw_index)
    {
        // The current raw line that we are on
        const struct pager_buffer_line *const
            rawline = &t->raw_lines[raw_index];

        gemtext.link = NULL;
        gemtext.raw_dist = 0;
        gemtext.hang = 0;
        gemtext.indent = 1;
        gemtext.esc = NULL;
        gemtext.esc_len = 0;
        *gemtext.prefix = 0;
        gemtext.prefix_len = 0;

        // Start a new line
        LINE_START();

        // Check for preformatting
        if (rawline->bytes >= strlen("```") &&
            strncmp(rawline->s, "```", strlen("```")) == 0)
        {
            // Empty line
            LINE_FINISH();
            gemtext.mode ^= PARSE_VERBATIM;
            continue;
        }

        if (gemtext.mode == PARSE_VERBATIM)
        {
            // Print text verbatim
            gemtext.indent = 2;
            LINE_INDENT(false);
            LINE_STRNCPY(rawline->s, rawline->bytes);
            LINE_FINISH();
            continue;
        }

        gemtext.mode = PARSE_PARAGRAPH;

        // Check for headings
        int heading_level = 0;
        for (const char *x = rawline->s;
            x < (rawline->s + rawline->bytes) && *x == '#';
            ++heading_level, ++x);
        switch(heading_level)
        {
        case 1:
            gemtext.esc = buffer_pos;
            const char *HEADING1_ESC = "\x1b[1;36m";
            LINE_STRNCPY_LIT(HEADING1_ESC);
            gemtext.esc_len = strlen(HEADING1_ESC);
            gemtext.raw_bytes_skip = 2;
            gemtext.need_clear_esc = true;
            break;

        case 2:
            gemtext.esc = buffer_pos;
            const char *HEADING2_ESC = "\x1b[36m";
            LINE_STRNCPY_LIT(HEADING2_ESC);
            gemtext.esc_len = strlen(HEADING1_ESC);
            gemtext.raw_bytes_skip = 3;
            gemtext.need_clear_esc = true;
            break;

        case 3:
            gemtext.esc = buffer_pos;
            const char *HEADING3_ESC = "\x1b[1m";
            LINE_STRNCPY_LIT(HEADING3_ESC);
            gemtext.esc_len = strlen(HEADING1_ESC);
            gemtext.raw_bytes_skip = 4;
            gemtext.need_clear_esc = true;
            break;

        default: break;
        }
        if (heading_level)
        {
            // For headings; we hang wrapped text by the first whitespace.
            // This makes e.g. documents with numbered sections look really
            // nice
            for (const char *i = rawline->s + heading_level + 1;
                i < rawline->s + rawline->bytes && *i;
                ++i)
            {
                if (*i == '\n' ||
                    *i == '\0')
                {
                    gemtext.hang = 0;
                    break;
                }
                if (*i == ' ' ||
                    *i == '\t')
                {
                    gemtext.hang = utf8_strnlen_w_formats(
                        rawline->s + heading_level,
                        i - rawline->s + heading_level);
                    break;
                }
            }

            line->is_heading = true;
            gemtext.indent = 0;
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
            bool has_title = false;
            for (const char *i = l_uri;
                i < rawline->s + rawline->bytes && *i;
                ++i)
            {
                if (*i == ' ' || *i == '\t')
                {
                    has_title = true;
                    break;
                }
            }
            if (has_title)
            {
                l_title = l_uri + l_uri_len;
                l_title += strspn(l_title, "\t ");
            }
            else
            {
                // Use URI as title
                l_title = l_uri;
            }

            // Add link to page state and set buffer location so we can
            // highlight on selection
            gemtext.link = &g_pager->links[l_index];
            gemtext.link->uri = uri_parse(l_uri, l_uri_len);
            uri_abs(&g_state->uri, &gemtext.link->uri);
            gemtext.link->buffer_loc = buffer_pos;
            gemtext.link->line_index = b->line_count;
            gemtext.link->buffer_loc_len = 0;

            // This is so that text wrapping will work for the link title.  We
            // want to print from where the title string starts if it exists
            // (if it doesn't we print from where the URI string is).
            gemtext.raw_bytes_skip = l_title - rawline->s;

            /*
             * This is a really subtle typographical feature; we align the link
             * prefix (via extra indent) with the rest of the links, when the
             * number of digits in the maximum link index is different
             *   e.g.:
             *      [8] This is a link that has an extra indent
             *      [9] This link has an extra indent too
             *     [10] Next llnk
             *     [11] Another llnk
             * Doesn't account for protocol name though.  Not sure if that
             * would be a good idea or not
             */
            gemtext.hang = 0;
            char l_index_str[16];
            int l_index_strlen = snprintf(l_index_str, sizeof(l_index_str),
                "%d", (int)l_index);
            gemtext.hang =
                max(gemtext.link_maxidx_strlen - l_index_strlen, 0);
            LINE_PRINTF("%*s", gemtext.hang, "");

            // Print link prefix
            //LINE_PRINTF(" [%d] ", (int)l_index);
            if (gemtext.link->uri.protocol != g_state->uri.protocol)
            {
                LINE_PRINTF(" [%s %s] ",
                    l_index_str, gemtext.link->uri.protocol_str);
                gemtext.hang += LINE_PRINTF_N_BYTES;
            }
            else
            {
                LINE_PRINTF(" [%s] ", l_index_str);
                gemtext.hang += LINE_PRINTF_N_BYTES;
            }

            // Alternative alphabetic list index, so you don't need to reach up
            // to the number row to select links
            //LINE_PRINTF(" [%c] ", 'a' + (int)l_index);
        }

        // Parse lists
        if (rawline->bytes > strlen("* ") &&
            strncmp(rawline->s, "* ", strlen("* ")) == 0)
        {
            gemtext.indent = 1;
            gemtext.mode = PARSE_LIST;
        }

        // Parse blockquotes
        if (rawline->bytes > strlen(">") &&
            strncmp(rawline->s, ">", strlen(">")) == 0)
        {
            gemtext.esc = buffer_pos;
            const char *BLOCKQUOTE_ESC = "\x1b[2m";
            LINE_STRNCPY_LIT(BLOCKQUOTE_ESC);
            gemtext.esc_len = strlen(BLOCKQUOTE_ESC);
            gemtext.need_clear_esc = true;
            gemtext.indent = 2;
            gemtext.mode = PARSE_BLOCKQUOTE;

            gemtext.raw_bytes_skip = strspn(rawline->s + 1, " ") + 1;
        }

        // Apply indent
        LINE_INDENT(false);

        if (gemtext.mode == PARSE_LIST)
        {
            // Apply our own bullet char to lists
            LINE_STRNCPY_LIT("\u2022");

            // Wrapped list lines get nicer indentation
            gemtext.indent = 3;

            // Skip over the asterisk char
            gemtext.raw_bytes_skip = 1;
        }
        else if (gemtext.mode == PARSE_BLOCKQUOTE)
        {
            // Set blockquote prefix (for both the initial and wrapped lines)
            LINE_STRNCPY_LIT("> ");
            strcpy(gemtext.prefix, "> ");
            gemtext.prefix_len = strlen(gemtext.prefix);
            gemtext.indent = 2;
        }

        // Margins are accounted for in "width total" here
        int width = width_total - gemtext.indent;

        // New line wrap method; iterate over each word in the line one-by-one,
        // and add them to the buffer.  This fixes the annoying infinite-loop
        // when a large word would span across lines.
        const char *c_prev = rawline->s + gemtext.raw_bytes_skip;
        gemtext.raw_bytes_skip = 0;
        int chars_this_column =
            utf8_strnlen_w_formats(line->s, buffer_pos - line->s);
        for (const char *c = c_prev;
            c <= rawline->s + rawline->bytes;
            ++c)
        {
            // Set the most recent escape code to print at beginning of next line
            if (*c == '\x1b')
            {
                const char *esc_start = c;

                // Get length of escape
                for (; c <= rawline->s + rawline->bytes; ++c)
                {
                    if (*c == 'm')
                    {
                        // End of escape code
                        gemtext.esc = esc_start;
                        gemtext.esc_len = c - esc_start + 1;
                        break;
                    }

                    if (!*c ||
                        c >= rawline->s + rawline->bytes ||
                        *c == ' ' ||
                        *c == '\n')
                    {
                        break;
                    }
                }
                continue;
            }

            // Found a space or we are at end of line
            if (*c == ' ' ||
                c == rawline->s + rawline->bytes)
            {
                while (chars_this_column + c - c_prev > width)
                {
                    // Fill in what we can of this column with the beginning of
                    // this huge word
                    int chars_count = max(width - chars_this_column - 1, 0);
                    if (chars_count > 8) // 8 is an alright number
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

                    // Clear the escape to make clearing text a bit cleaner
                    LINE_STRNCPY_LIT("\x1b[0m");

                    // Start next line
                    LINE_FINISH();
                    chars_this_column = gemtext.indent + gemtext.hang;
                    LINE_START();
                    LINE_INDENT(true);

                    // Apply the escape if we had one
                    if (gemtext.esc)
                    {
                        LINE_STRNCPY(gemtext.esc, gemtext.esc_len);
                    }

                    // Apply prefix if we have one
                    if (*gemtext.prefix)
                    {
                        LINE_STRNCPY(gemtext.prefix, gemtext.prefix_len);
                    }

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

        if (gemtext.need_clear_esc)
        {
            LINE_STRNCPY_LIT("\x1b[0m");
            gemtext.need_clear_esc = false;
            gemtext.esc = NULL;
            gemtext.esc_len = 0;
        }

        LINE_FINISH();
    }

    return buffer_pos - b->b;

#undef LINE_START
#undef LINE_FINISH
#undef LINE_INDENT
#undef LINE_STRNCPY
#undef LINE_STRNCPY_LIT
#undef LINE_PRINTF
}

/* Typeset plaintext to a pager buffer */
static size_t
typeset_plaintext(
    struct typesetter *t,
    struct pager_buffer *b)
{
    // Position in buffer that we're up to
    char *buffer_pos = b->b;

    // Iterate over each of the raw lines in the buffer
    struct pager_buffer_line *line = b->lines;
    const char *const buffer_end_pos = b->b + b->size;
    for (int raw_index = 0;
        raw_index < t->raw_line_count;
        ++raw_index)
    {
        // The current raw line that we are on
        const struct pager_buffer_line *const
            rawline = &t->raw_lines[raw_index];

        // Plaintext is always rendered verbatim
        line->s = buffer_pos;
        line->bytes = 0;
        line->raw_index = raw_index;
        line->raw_dist = 0;

        const size_t n_bytes = rawline->bytes;
        BUFFER_CHECK_SIZE(n_bytes);
        strncpy(buffer_pos,
            rawline->s,
            min(n_bytes, buffer_end_pos - buffer_pos));
        line->bytes += n_bytes;
        buffer_pos += n_bytes;

        // Finish line
        line->len = utf8_strnlen_w_formats(line->s, line->bytes);
        if ((b->line_count + 1) * sizeof(struct pager_buffer_line) >=
            b->lines_capacity)
        {
            return 0;
        }
        ++b->line_count;
        ++line;
    }

    return buffer_pos - b->b;
}


/* Typeset a gophermap */
static size_t
typeset_gophermap(
    struct typesetter *t,
    struct pager_buffer *b)
{
    // Position in buffer that we're up to
    char *buffer_pos = b->b;

    struct uri uri;

    // Set URI protocol
    uri.protocol = PROTOCOL_GOPHER;
    strcpy(uri.protocol_str, "gopher");

    // Helper macros
#define LINE_STRNCPY(str, len) \
    do \
    { \
        const size_t n_bytes = (len); \
        BUFFER_CHECK_SIZE(n_bytes); \
        strncpy(buffer_pos, \
            (str), \
            min(n_bytes, buffer_end_pos - buffer_pos)); \
        line->bytes += n_bytes; \
        buffer_pos += n_bytes; \
    } while(0)
#define LINE_STRNCPY_LIT(str) LINE_STRNCPY((str), strlen((str)))
#define LINE_PRINTF(fmt, ...) \
    do \
    { \
        const size_t n_bytes = \
            snprintf(buffer_pos, \
                buffer_end_pos - buffer_pos, \
                (fmt), \
                __VA_ARGS__); \
        BUFFER_CHECK_SIZE(n_bytes); \
        line->bytes += n_bytes; \
        buffer_pos += n_bytes; \
    } while(0);
#define ADD_LINK() \
    {\
        pager_check_link_capacity(); \
        l_index = g_pager->link_count++; \
        struct pager_link *link = &g_pager->links[l_index]; \
        memcpy(&link->uri, &uri, sizeof(struct uri)); \
        link->line_index = b->line_count; \
        link->buffer_loc = buffer_pos; \
        link->buffer_loc_len = item_display_len; \
    }

    // Iterate over each of the raw lines in the buffer
    struct pager_buffer_line *line = b->lines;
    const char *const buffer_end_pos = b->b + b->size;
    for (int raw_index = 0;
        raw_index < t->raw_line_count;
        ++raw_index)
    {
        // The current raw line that we are on
        const struct pager_buffer_line *const
            rawline = &t->raw_lines[raw_index];

        line->s = buffer_pos;
        line->bytes = 0;
        line->raw_index = raw_index;
        line->raw_dist = 0;

        // Write display string to the buffer
        const char *item_display = rawline->s + 1;
        const size_t item_display_len = strcspn(item_display, "\t");

        // Get item path
        const char *item_path = item_display + item_display_len + 1;
        const size_t item_path_len = strcspn(item_path, "\t");
        strncpy(uri.path, item_path, min(item_path_len, URI_PATH_MAX));
        uri.path[item_path_len] = '\0';

        // Get item hostname
        const char *item_hostname = item_path + item_path_len + 1;
        const size_t item_hostname_len = strcspn(item_hostname, "\t");
        strncpy(uri.hostname, item_hostname,
            min(item_hostname_len, URI_HOSTNAME_MAX));
        uri.hostname[item_hostname_len] = '\0';

        // Get item port
        uri.port = atoi(item_hostname + item_hostname_len + 1);

        // Check item type for selector
        size_t l_index = 0;
        switch(*rawline->s)
        {
        // Informational message
        case 'i':
            break;

        // Error code to indicate failure
        case '3':
            break;

        /* All types below are displayed as links */

        // Document
        case 'd':
            ADD_LINK();
            LINE_PRINTF(" [%d document] ", (int)l_index);
            break;

        // HTML file
        case 'h':
            ADD_LINK();
            LINE_PRINTF(" [%d html] ", (int)l_index);
            break;

        // Sound file
        case 's':
            ADD_LINK();
            LINE_PRINTF(" [%d sound] ", (int)l_index);
            break;

        // File (usually text)
        case '0':
            ADD_LINK();
            LINE_PRINTF(" [%d text] ", (int)l_index);
            break;

        // Directory
        case '1':
            ADD_LINK();
            LINE_PRINTF(" [%d dir] ", (int)l_index);
            break;

        // Binary files
        case '9':
        case 'I':
        case '2':
            ADD_LINK();
            LINE_PRINTF(" [%d binary] ", (int)l_index);
            break;

        // Unknown/unsupported item type
        default:
            ADD_LINK();
            LINE_PRINTF(" [%d unsupported] ", (int)l_index);
            break;
        }

        // Write display string
        LINE_STRNCPY(item_display, item_display_len);

        // Finish line
        line->len = utf8_strnlen_w_formats(line->s, line->bytes);
        if ((b->line_count + 1) * sizeof(struct pager_buffer_line) >=
            b->lines_capacity)
        {
            return 0;
        }
        ++b->line_count;
        ++line;
    }

    return buffer_pos - b->b;

#undef LINE_STRNCPY
#undef LINE_STRNCPY_LIT
#undef LINE_PRINTF
#undef ADD_LINK
}

bool
typeset_page(
    struct typesetter *t,
    struct pager_buffer *b,
    size_t w,
    struct mime *m)
{
    // Bytes written by typesetter
    size_t n_bytes;

    if (mime_eqs(m, MIME_GEMTEXT))
    {
        // Typeset gemtext document
        if (!typeset_start(t, b, w)) return false;
        n_bytes = typeset_gemtext(t, b, w);
        typeset_finish(b, n_bytes);
        return true;
    }

    if (mime_eqs(m, MIME_GOPHERMAP))
    {
        if (!typeset_start(t, b, w)) return false;
        n_bytes = typeset_gophermap(t, b);
        typeset_finish(b, n_bytes);
        return true;
    }

    if (mime_eqs(m, MIME_PLAINTEXT))
    {
        // Typeset plaintext document
        if (!typeset_start(t, b, w)) return false;
        n_bytes = typeset_plaintext(t, b);
        typeset_finish(b, n_bytes);
        return true;
    }

    return false;
}
