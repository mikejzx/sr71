#ifndef UTF8_H
#define UTF8_H

/*
 * utf8.h
 *
 * Code for handling multi-byte sequences
 */

void utf8_init(void);
void utf8_deinit(void);

/* Calculate width of a string with multi-byte sequences */
size_t utf8_width(const char *, size_t);

/* Number of bytes that exist in a length (inverse of above) */
size_t utf8_size_w_formats(const char *, size_t);

/*
 * Move to next character in a string that has escapes codes in it, while
 * ignoring escapes
 */
const char *next_char_w_formats(const char *restrict, const char *restrict);

#endif
