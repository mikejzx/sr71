#ifndef GOPHER_H
#define GOPHER_H

struct uri;

/*
 * gopher.h
 *
 * Gopher client code
 */

struct gopher
{
    int sock;
};

void gopher_deinit(void);

// Issue request over gopher
int gopher_request(struct uri *);

#endif
