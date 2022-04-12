#ifndef UTIL_H
#define UTIL_H

#define min(x, y) \
    ((x) < (y) ? (x) : (y))
#define max(x, y) \
    ((x) > (y) ? (x) : (y))
#define sign(x) \
    ((x) > 0 ? 1 : -1)

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

/* Move to beginning of next character in UTF-8 string (ignores formats) */
//const char *utf8_next_char(const char *restrict, const char *restrict);

/*
 * Move to next character in a string that has escapes codes in it (ignores
 * escapes)
 */
const char *next_char_w_formats(const char *restrict, const char *restrict);

int connect_socket_to(const char *, int port);

/*
 * Convert time since timestamp to a human-readable string.
 * Excuse the abyssmal function name
 */
int timestamp_age_human_readable(time_t, char *, size_t);

#endif
