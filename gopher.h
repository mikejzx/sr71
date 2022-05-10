#ifndef GOPHER_H
#define GOPHER_H

#include "mime.h"

struct uri;

enum gopher_item_type
{
    GOPHER_ITEM_UNSUPPORTED = -1,
    GOPHER_ITEM_DIR = 0,
    GOPHER_ITEM_TEXT,
    GOPHER_ITEM_BIN,
    GOPHER_ITEM_SEARCH,

    GOPHER_ITEM_COUNT
};

// Chars that represent each Gopher item type
static const char GOPHER_ITEM_IDS[GOPHER_ITEM_COUNT] =
{
    [GOPHER_ITEM_DIR] = '1',
    [GOPHER_ITEM_TEXT] = '0',
    [GOPHER_ITEM_SEARCH] = '7',
    [GOPHER_ITEM_BIN] = '0',
};

static inline enum gopher_item_type
gopher_item_lookup(char c)
{
    for (enum gopher_item_type i = 0; i < GOPHER_ITEM_COUNT; ++i)
    {
        if (GOPHER_ITEM_IDS[i] == c) return i;
    }
    return GOPHER_ITEM_UNSUPPORTED;
}

// MIME types that can correspond with gopher items
static const char *GOPHER_ITEM_MIME[GOPHER_ITEM_COUNT] =
{
    [GOPHER_ITEM_DIR]    = MIME_GOPHERMAP,
    [GOPHER_ITEM_TEXT]   = MIME_PLAINTEXT,
    [GOPHER_ITEM_BIN]    = "",
    [GOPHER_ITEM_SEARCH] = MIME_GOPHERMAP,
};

/* Get MIME type from Gopher item */
static inline const char *
gopher_item_to_mime(enum gopher_item_type item)
{
    // We default to gophermap
    if (item < 0 || item >= GOPHER_ITEM_COUNT) item = GOPHER_ITEM_DIR;
    return GOPHER_ITEM_MIME[item];
}

/* Convert MIME type to gopher item */
static inline enum gopher_item_type
gopher_mime_to_item(struct mime *mime)
{
    if (mime_eqs(mime, MIME_GOPHERMAP)) return GOPHER_ITEM_DIR;
    if (mime_eqs(mime, MIME_PLAINTEXT)) return GOPHER_ITEM_TEXT;
    return GOPHER_ITEM_UNSUPPORTED;
}

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
