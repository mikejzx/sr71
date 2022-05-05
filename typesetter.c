#include "pch.h"
#include "gemini.h"
#include "line_break_alg.h"
#include "mime.h"
#include "pager.h"
#include "state.h"
#include "tui.h"
#include "typesetter.h"

void
typesetter_init(struct typesetter *t)
{
    memset(t, 0, sizeof(struct typesetter));

    line_break_init();
}

void
typesetter_deinit(struct typesetter *t)
{
    if (t->raw_lines) free(t->raw_lines);

    line_break_deinit();
}

/* Initialise typesetter with raw content data */
void
typesetter_reinit(struct typesetter *t)
{
    // Select the buffer (use alt if available)
    const char *rawbuf = g_recv->b_alt ? g_recv->b_alt : g_recv->b;

    // Count lines
    size_t old_line_count = t->raw_line_count;
    t->raw_line_count = 0;
    for (int i = 0; i < g_recv->size; ++i)
    {
        if (rawbuf[i] == '\n' || i == g_recv->size - 1) ++t->raw_line_count;
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
    const char *c, *end = rawbuf + g_recv->size, *start = rawbuf;
    struct pager_buffer_line *line = t->raw_lines;
    for (c = start; c < end; ++c)
    {
        if (*c != '\n' && c != end - 1) continue;

        line->s = start;
        line->bytes = c - start;

        for (const char *j = c - 1;
            j >= start && strchr("\r\n \t", *j) != NULL && line->bytes > 0;
            --j, --line->bytes);

        line->len = utf8_width(line->s, line->bytes);

        start = c + 1;
        ++line;
    }
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
        b->size = (g_recv->size * 10) / 2;
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

        // Current indentation amount
        int indent;

        // A "canonical" indent, one that is literally pushed into the buffer
        // (whereas above is applied during rendering).
        int indent_canon;

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

        // Prefix applied to wrapped lines
        char prefix[8];
        size_t prefix_len;

        // Whether last non-empty line was a heading.
        bool last_was_heading;
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
        line->is_hyphenated = false; \
        line->prefix_len = 0; \
    } while(0)
#define LINE_FINISH() \
    do \
    { \
        if (!line_started) break; \
        line_started = false; \
        line->bytes = buffer_pos - line->s; \
        line->len = utf8_width(line->s, line->bytes); \
        line->raw_dist = gemtext.raw_dist; \
        line->indent = gemtext.indent; \
        if ((b->line_count + 1) * sizeof(struct pager_buffer_line) >= \
            b->lines_capacity) \
        { \
            /* just abort if line count gets stupid (due to tiny widths) */ \
            return 0; \
        } \
        ++b->line_count; \
        ++line; \
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
    const char *rawline_end;
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
        gemtext.indent = GEMTEXT_INDENT_PARAGRAPH;
        gemtext.esc = NULL;
        gemtext.esc_len = 0;
        *gemtext.prefix = 0;
        gemtext.prefix_len = 0;
        rawline_end = rawline->s + rawline->bytes;

    #if GEMTEXT_FANCY_PARAGRAPH_INDENT
        gemtext.indent_canon =
        #if GEMTEXT_FANCY_PARAGRAPH_INDENT_ALWAYS
            // Constant paragraph indent
            GEMTEXT_FANCY_PARAGRAPH_INDENT;
        #else
            // Paragraphs except first under a heading get a nice little indent
            gemtext.last_was_heading ? 0 : GEMTEXT_FANCY_PARAGRAPH_INDENT;
        #endif
    #else
        gemtext.indent_canon = 0;
    #endif

        // Start a new line
        LINE_START();

        // Check for empty lines
        if (rawline->bytes == 0)
        {
            LINE_FINISH();
            continue;
        }

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
            line->indent = GEMTEXT_INDENT_VERBATIM;
            LINE_STRNCPY(rawline->s, rawline->bytes);
            LINE_FINISH();
            continue;
        }

        gemtext.mode = PARSE_PARAGRAPH;

        // Check for headings
        int heading_level = 0;
        for (const char *x = rawline->s;
            x < rawline_end && *x == '#';
            ++heading_level, ++x);
        switch(heading_level)
        {
        case 1:
            gemtext.esc = buffer_pos;
            LINE_STRNCPY_LIT(COLOUR_HEADING1);
            gemtext.esc_len = buffer_pos - gemtext.esc;
            gemtext.raw_bytes_skip = 2;
            break;

        case 2:
            gemtext.esc = buffer_pos;
            LINE_STRNCPY_LIT(COLOUR_HEADING2);
            gemtext.esc_len = buffer_pos - gemtext.esc;
            gemtext.raw_bytes_skip = 3;
            break;

        case 3:
            gemtext.esc = buffer_pos;
            LINE_STRNCPY_LIT(COLOUR_HEADING3);
            gemtext.esc_len = buffer_pos - gemtext.esc;
            gemtext.raw_bytes_skip = 4;
            break;

        // Level 4 headings are non-standard, but we support them nevertheless
        // as there are some pages which decide to use them anyway
        case 4:
            gemtext.esc = buffer_pos;
            LINE_STRNCPY_LIT(COLOUR_HEADING4);
            gemtext.esc_len = buffer_pos - gemtext.esc;
            gemtext.raw_bytes_skip = 5;
            break;

        default: break;
        }
        if (heading_level)
        {
            // For headings; we hang wrapped text by the first whitespace.
            // This makes e.g. documents with numbered sections look really
            // nice
            for (const char *i = rawline->s + heading_level + 1;
                i <= rawline_end && *i;
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
                    for (;
                        *i && (*i == ' ' || *i == '\t') && i <= rawline_end;
                        ++i);
                    gemtext.hang = utf8_width(
                        rawline->s + heading_level + 1,
                        i - (rawline->s + heading_level + 1));
                    break;
                }
            }

            line->is_heading = true;
            gemtext.indent = GEMTEXT_INDENT_HEADING;
            gemtext.indent_canon = 0;
            gemtext.last_was_heading = true;
        }
        else if (rawline->bytes >= width_total / 2)
        {
            gemtext.last_was_heading = false;
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
                i < rawline_end && *i;
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
            gemtext.indent = 0;
            gemtext.indent_canon = 0;

            goto wrap;
        }

        // Parse lists
        if (rawline->bytes > strlen("* ") &&
            strncmp(rawline->s, "* ", strlen("* ")) == 0)
        {
            gemtext.mode = PARSE_LIST;

            // Apply our own bullet char to lists
            LINE_STRNCPY_LIT(LIST_BULLET_CHAR);

            // Wrapped list lines get nicer indentation
            gemtext.indent = GEMTEXT_INDENT_LIST;
            gemtext.indent_canon = 0;
            gemtext.hang =
                utf8_width(LIST_BULLET_CHAR, LIST_BULLET_CHAR_LEN);

            // Skip over the asterisk char
            gemtext.raw_bytes_skip = 2;
        }
        // Parse blockquotes
        else if (rawline->bytes > strlen(">") &&
            strncmp(rawline->s, ">", strlen(">")) == 0)
        {
            gemtext.mode = PARSE_BLOCKQUOTE;
            gemtext.esc = buffer_pos;
            const char *BLOCKQUOTE_ESC = "\x1b[2m";
            LINE_STRNCPY_LIT(BLOCKQUOTE_ESC);
            gemtext.esc_len = strlen(BLOCKQUOTE_ESC);

            // Set blockquote prefix (for both the initial and wrapped lines)
            LINE_STRNCPY_LIT(BLOCKQUOTE_PREFIX);
            strcpy(gemtext.prefix, BLOCKQUOTE_PREFIX);
            gemtext.indent = GEMTEXT_INDENT_BLOCKQUOTE;
            gemtext.indent_canon = 0;
            line->prefix_len = gemtext.prefix_len;
            gemtext.prefix_len = strlen(BLOCKQUOTE_PREFIX);

            gemtext.raw_bytes_skip = strspn(rawline->s + 1, " ") + 1;
        }

    wrap: ;
        // Margins are accounted for in "width total" here
        int width = width_total - gemtext.indent;

        /* Line breaking */
        struct lb_prepare_args pa =
        {
            .line = rawline->s,
            .line_end = rawline->s + rawline->bytes,
            .length = width,
            .offset = gemtext.raw_bytes_skip,
            .indent = gemtext.indent_canon,
            .hang = gemtext.hang,
            // A bit hacky; we need to skip over the inserted escape codes for
            // headings to wrap at correct point.
            .skip = line->is_heading
                ? 0
                : utf8_width(line->s, buffer_pos - line->s),
        };
        line_break_prepare(&pa);
    #if TYPESET_LINEBREAK_GREEDY
        line_break_compute_greedy();
    #else
        line_break_compute_knuth_plass();
    #endif

        gemtext.raw_bytes_skip = 0;

        // TODO: buffer size check
        //BUFFER_CHECK_SIZE(width * 4 * );

        int indent_no_hang = gemtext.indent;

        // Write the broken lines into the buffer
        ssize_t len;
        for (; line_break_has_data();)
        {
            if (!line_started)
            {
                LINE_START();

                gemtext.indent = indent_no_hang + gemtext.hang;

                // Apply the escape if we had one
                if (gemtext.esc)
                {
                    LINE_STRNCPY(gemtext.esc, gemtext.esc_len);
                    line->prefix_len += gemtext.esc_len;
                }

                // Apply prefix if we have one
                if (*gemtext.prefix)
                {
                    LINE_STRNCPY(gemtext.prefix, gemtext.prefix_len);
                    line->prefix_len += gemtext.prefix_len;
                }
            }
            len = line_break_get(buffer_pos, buffer_end_pos - buffer_pos);

            buffer_pos += len;
            gemtext.raw_dist += len;
            if (gemtext.link) gemtext.link->buffer_loc_len += len;

            LINE_FINISH();
        }

        LINE_FINISH();
    }

    return buffer_pos - b->b;

#undef LINE_START
#undef LINE_FINISH
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
        line->raw_index = raw_index;
        line->raw_dist = 0;
        line->is_heading = false;
        line->is_hyphenated = false;
        line->indent = 0;
        line->prefix_len = 0;

        if (rawline->bytes < 1)
        {
            line->bytes = 0;
            line->len = 0;
            goto next_line;
        }

        const size_t n_bytes = rawline->bytes;
        BUFFER_CHECK_SIZE(n_bytes);
        strncpy(buffer_pos,
            rawline->s,
            min(n_bytes, buffer_end_pos - buffer_pos));
        buffer_pos += n_bytes;

        // Finish line
        line->len = rawline->len;
        line->bytes = n_bytes;

    next_line:
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

    // Set no query to the URI
    uri.query[0] = '\0';

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
    const char *rawline_end;
    for (int raw_index = 0;
        raw_index < t->raw_line_count;
        ++raw_index)
    {
        // The current raw line that we are on
        const struct pager_buffer_line *const
            rawline = &t->raw_lines[raw_index];

        if (rawline->bytes < 1) continue;

        line->s = buffer_pos;
        line->bytes = 0;
        line->raw_index = raw_index;
        line->raw_dist = 0;
        line->is_heading = false;
        line->is_hyphenated = false;
        line->indent = 0;
        line->prefix_len = 0;
        rawline_end = rawline->s + rawline->bytes;

        // Write display string to the buffer
        const char *item_display = rawline->s + 1;
        const size_t item_display_len = strcspn(item_display, "\t");
        if (item_display + item_display_len >= rawline_end) continue;

        // Get item path
        const char *item_path = item_display + item_display_len + 1;
        const size_t item_path_len = strcspn(item_path, "\t");
        if (item_path + item_path_len >= rawline_end) continue;
        strncpy(uri.path, item_path, min(item_path_len, URI_PATH_MAX));
        uri.path[item_path_len] = '\0';

        // Get item hostname
        const char *item_hostname = item_path + item_path_len + 1;
        const size_t item_hostname_len = strcspn(item_hostname, "\t");
        if (item_hostname + item_hostname_len + 1 >= rawline_end) continue;
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
            uri.gopher_item = GOPHER_ITEM_UNSUPPORTED;
            ADD_LINK();
            LINE_PRINTF(" [%d doc] ", (int)l_index);
            break;

        // HTML file
        case 'h':
            uri.gopher_item = GOPHER_ITEM_UNSUPPORTED;
            ADD_LINK();
            LINE_PRINTF(" [%d html] ", (int)l_index);
            break;

        // Sound file
        case 's':
            uri.gopher_item = GOPHER_ITEM_UNSUPPORTED;
            ADD_LINK();
            LINE_PRINTF(" [%d snd] ", (int)l_index);
            break;

        // File (usually text)
        case '0':
            uri.gopher_item = GOPHER_ITEM_TEXT;
            ADD_LINK();
            LINE_PRINTF(" [%d txt] ", (int)l_index);
            break;

        // Directory
        case '1':
            uri.gopher_item = GOPHER_ITEM_DIR;
            if (item_path[item_path_len] != '/' &&
                item_path_len + 1 < URI_PATH_MAX)
            {
                strcat(uri.path, "/");
            }
            ADD_LINK();
            LINE_PRINTF(" [%d dir] ", (int)l_index);
            break;

        // Binary files
        case '9':
        case 'I':
        case '2':
            uri.gopher_item = GOPHER_ITEM_BIN;
            ADD_LINK();
            LINE_PRINTF(" [%d bin] ", (int)l_index);
            break;

        // Unknown/unsupported item type
        default:
            uri.gopher_item = GOPHER_ITEM_UNSUPPORTED;
            ADD_LINK();
            LINE_PRINTF(" [%d unsupported] ", (int)l_index);
            break;
        }

        // Write display string
        LINE_STRNCPY(item_display, item_display_len);

        // Finish line
        line->len = utf8_width(line->s, line->bytes);
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

    t->content_width = w;

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
