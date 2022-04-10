#ifndef TYPESETTER_H
#define TYPESETTER_H

struct pager_buffer;
struct pager_buffer_line;
struct mime;

/*
 * typesetter.h
 *
 * Handles typesetting/rendering of documents to something that the pager can
 * nicely display
 */

struct typesetter
{
    // These lines point to the raw buffer itself
    struct pager_buffer_line *raw_lines;
    int raw_line_count;

    // Current content width
    int content_width;
};

void typesetter_init(struct typesetter *);
void typesetter_deinit(struct typesetter *);
void typesetter_reinit(struct typesetter *);
bool typeset_page(struct typesetter *,
    struct pager_buffer *, size_t, struct mime *);

#endif
