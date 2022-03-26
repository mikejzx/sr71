#ifndef UTIL_H
#define UTIL_H

#define min(x, y) \
    ((x) < (y) ? (x) : (y))
#define max(x, y) \
    ((x) > (y) ? (x) : (y))

/* Normalises two paths */
int path_normalise(const char *restrict, const char *restrict, char *restrict);

/* Count number of code points in UTF-8 string */
size_t utf8_strlen(const char *);

/* Same as above, but with fixed size */
size_t utf8_strnlen(const char *, size_t);

/* Also ignores formatting */
size_t utf8_strnlen_w_formats(const char *, size_t);

/* Number of bytes that exist in a length (inverse of above) */
size_t utf8_size_w_formats(const char *, size_t);

#endif
