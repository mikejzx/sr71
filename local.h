#ifndef LOCAL_H
#define LOCAL_H

struct uri;

// Make a file:// request to local filesystem
int local_request(const struct uri *restrict const, int *restrict);

#endif
