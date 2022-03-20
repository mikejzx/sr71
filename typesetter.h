#ifndef TYPESETTER_H
#define TYPESETTER_H

struct pager_buffer;

/*
 * typesetter.h
 *
 * Handles typesetting/rendering of documents to something that the pager can
 * nicely display
 */

struct typesetter
{
    // Current raw buffer in the typesetter
    const char *raw;
    size_t raw_size;

    // And lines; their strings point to the raw buffer above
    struct pager_buffer_line *raw_lines;
    size_t raw_line_count;
};

void typesetter_init(struct typesetter *);
void typesetter_deinit(struct typesetter *);
void typesetter_reinit(struct typesetter *, const char *, size_t);
void typeset_gemtext(struct typesetter *, struct pager_buffer *, size_t);
//void typeset_gophermap(const char *, struct pager_buffer *);

#endif
