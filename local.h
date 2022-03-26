#ifndef LOCAL_H
#define LOCAL_H

struct uri;

// Make a file:// request to local filesystem
int local_request(struct uri *, int *);

#endif
