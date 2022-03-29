#ifndef GOPHER_H
#define GOPHER_H

struct uri;

enum gopher_item_type
{
    GOPHER_ITEM_UNSUPPORTED = -1,
    GOPHER_ITEM_DIR = 0,
    GOPHER_ITEM_TEXT,
    GOPHER_ITEM_BIN,
};

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
