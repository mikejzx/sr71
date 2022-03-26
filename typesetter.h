#ifndef TYPESETTER_H
#define TYPESETTER_H

struct pager_buffer;
struct mime;

/*
 * typesetter.h
 *
 * Handles typesetting/rendering of documents to something that the pager can
 * nicely display
 */

struct typesetter
{
    // And lines; their strings point to the raw buffer above
    struct pager_buffer_line *raw_lines;
    size_t raw_line_count;
};

void typesetter_init(struct typesetter *);
void typesetter_deinit(struct typesetter *);
void typesetter_reinit(struct typesetter *);
bool typeset_page(struct typesetter *,
    struct pager_buffer *, size_t, struct mime *);

#endif
