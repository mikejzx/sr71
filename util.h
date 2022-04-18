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

int connect_socket_to(const char *, int port);

/*
 * Convert time since timestamp to a human-readable string.
 * Excuse the abyssmal function name
 */
int timestamp_age_human_readable(time_t, char *, size_t);

#endif
