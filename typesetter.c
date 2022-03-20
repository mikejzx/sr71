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
typesetter_reinit(
    struct typesetter *t,
    const char *content,
    size_t content_size)
{
    t->raw = content;
    t->raw_size = content_size;

    // Count lines
    size_t old_line_count = t->raw_line_count;
    t->raw_line_count = 0;
    for (int i = 0; i < content_size; ++i)
    {
        if (t->raw[i] == '\n' || i == content_size - 1) ++t->raw_line_count;
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
    for (int i = 0; i < content_size; ++i)
    {
        if (t->raw[i] != '\n' && i != content_size - 1) continue;

        t->raw_lines[l].s = &t->raw[l_prev + 1];
        t->raw_lines[l].bytes = i - l_prev - 1;
        t->raw_lines[l].len = utf8_strnlen_w_formats(
            &t->raw[l_prev + 1],
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
    // Allocate space for buffer if needed
    if (!b->b)
    {
        b->b = malloc(t->raw_size);
    }
    else if (b->size < t->raw_size)
    {
        b->size = t->raw_size;
        void *tmp = realloc(b->b, b->size);
        if (!tmp)
        {
            free(b->b);
            exit(-1);
        }
        b->b = tmp;
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
    if (!b->lines)
    {
        b->lines = malloc(wanted_lines_size);
        b->lines_capacity = wanted_lines_size;
    }
    else if (b->lines_capacity < wanted_lines_size)
    {
        free(b->lines);
        b->lines = malloc(wanted_lines_size);
        b->lines_capacity = wanted_lines_size;
    }

    // Nicely typeset the gemtext
    b->line_count = 0;
    char *line_pos = b->b;
    bool is_preformatted = false, is_list = false;
    int indent = 0;
    for (int raw_index = 0; raw_index < t->raw_line_count; ++raw_index)
    {
        // Raw line
        struct pager_buffer_line *rl = &t->raw_lines[raw_index];

        // Distance that line is from beginning of raw line
        size_t raw_dist = 0;

        // Parse links
        if (strncmp(rl->s, "=>", strlen("=>")) == 0)
        {
            // Don't overflow link capacity
            if (g_pager->link_count + 1 > MAX_LINKS)
            {
                // Max links exceeded
                // TODO: don't just abort here and instead reallocate or
                // something
                continue;
            }
            size_t link_index = g_pager->link_count++;

            // Get the URI
            const char *link_uri = rl->s + strspn(rl->s, "=> \t");
            size_t link_uri_len = strcspn(link_uri, "\t ");

            // Get optional human-friendly label
            const char *link_title = link_uri + link_uri_len;
            link_title += strspn(link_title, "\t ");
            size_t link_title_len = rl->s + rl->bytes - link_title;
            if (link_title_len == 0)
            {
                link_title = link_uri;
                link_title_len = link_uri_len;
            }

            // Generate final link display
            size_t link_len = snprintf(
                line_pos,
                b->b + b->size - line_pos,
                " [%d] %.*s",
                (int)link_index,
                (int)link_title_len, link_title);

            // Add link to the page state
            struct pager_link *link = &g_pager->links[link_index];
            link->uri = uri_parse(link_uri, link_uri_len);
            uri_abs(&g_state->gem.uri, &link->uri);
            link->buffer_loc = line_pos;
            link->buffer_loc_len = link_len;
            link->line_index = b->line_count;

            // Copy string
            b->lines[b->line_count++] = (struct pager_buffer_line)
            {
                .s = line_pos,
                .len = utf8_strnlen_w_formats(line_pos, link_len),
                .bytes = link_len,
                .raw_index = raw_index,
                .raw_dist = raw_dist,
            };
            line_pos += link_len;
            raw_dist += link_len;
            continue;
        }

        // Pretty list indentation
        if (strncmp(rl->s, "* ", strlen("* ")) == 0)
        {
            if (!is_list)
            {
                is_list = true;
                indent = 1;

                // Copy in the bullet character
            }
        }
        else
        {
            if (is_list)
            {
                is_list = false;
                indent = 0;
            }
        }

        // New width that depends on indent level
        int width = width_total - indent;

        // Left-align/wrap the text
        const char *i, *i_prev = rl->s;
        for (i = rl->s + width;
            i < rl->s + rl->bytes;
            i += width)
        {
            // Go to the end of the last word that would be on this line
            for (; *i != ' ' && i > rl->s; --i);

            if (i == i_prev) continue;

            // Copy this line section
            size_t len = i - i_prev;
            if (line_pos + len + 1 >= b->b + b->size) goto abort;
            strncpy(line_pos, i_prev, len);
            b->lines[b->line_count++] = (struct pager_buffer_line)
            {
                .s = line_pos,
                .len = utf8_strnlen_w_formats(line_pos, len),
                .bytes = len,
                .raw_index = raw_index,
                .raw_dist = raw_dist,
            };
            line_pos += len;
            raw_dist += len;

            i_prev = i;

            // Strip leading space on broken line
            for (; *i_prev == ' ' && i_prev < rl->s + rl->bytes; ++i_prev);
        }

        // Add the remainder of text
        {
            size_t len = rl->s + rl->bytes - i_prev;
            if (line_pos + len + 1 >= b->b + b->size) goto abort;
            strncpy(line_pos, i_prev, len);
            b->lines[b->line_count++] = (struct pager_buffer_line)
            {
                .s = line_pos,
                .len = utf8_strnlen_w_formats(line_pos, len),
                .bytes = len,
                .raw_index = raw_index,
                .raw_dist = raw_dist,
            };
            line_pos += len;
            raw_dist += len;
        }
    }

    // Replace all tabs with spaces, as we can't render them properly
    for (char *c = b->b; c < b->b + b->size; ++c)
    {
        if (*c == '\t') *c = ' ';
    }

    return;
abort:
    // Out of buffer space! TODO avoid this somehow
    tui_cmd_status_prepare();
    tui_say("typeset failed: out of buffer space! (FIXME)");
}
