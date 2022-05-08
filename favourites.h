#ifndef FAVOURITES_H
#define FAVOURITES_H

#define FAVOURITE_TITLE_MAX (32)

#include "state.h"

struct uri;

/*
 * favourites.h
 *
 * Favourites are loaded from a file that is also written to on exit.  We read
 * it into a simple doubly-linked list into memory.
 *
 * Categories/folders are not implemented for the moment, but in future it
 * should be fairly straight-forward to implement; just store the list heads
 * under another list of "categories"
 */

struct fav_node
{
    // Next/prev node in the list
    struct fav_node *link_n, *link_p;

    // Store the URI as a null-terminated dynamically-allocated string, so we
    // don't need maximum URI size for every node, as most of that space will
    // be unused.
    char *uri;

    // Name of the favourite
    char title[FAVOURITE_TITLE_MAX];
};

void favourites_init(void);
void favourites_deinit(void);

// Displays favourites in a buffer
int favourites_display(void);

struct fav_node *favourites_find(const struct uri *);
void favourites_push(struct fav_node *);
struct fav_node *favourites_push_uri(
    const struct uri *restrict, const char *restrict, int);
void favourites_delete(struct fav_node *);
void favourites_update_title(
    struct fav_node *restrict, const char *restrict, int);

static inline bool
favourites_has(const struct uri *uri)
{
    return !!favourites_find(uri);
}

static inline bool
favourites_is_viewing(void)
{
    return strncmp(g_state->uri.hostname,
        URI_INTERNAL_FAVOURITES_RAW,
        URI_HOSTNAME_MAX) == 0;
}

#endif
