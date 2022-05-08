#ifndef HISTORY_H
#define HISTORY_H

#include "uri.h"

#define MAX_HISTORY_SIZE 64

/*
 * history.h
 *
 * "History" refers to two things;
 * + The undo/redo history stack, for the current session
 * + The history "log"; a persistent & continually-running log of all visited
 *   pages, with timestamps.
 */

struct history_item
{
    // Whether this history item has been properly initialised with a URI
    bool initialised;

    // URI the item points to
    struct uri uri;
};

/*
 * The history stack is a kind of preallocated stack structure.  All the memory
 * is allocated once, and when it is about to overflow the base pointer is
 * moved back to the beginning.
 */
struct history_stack
{
    // History items themselves
    struct history_item *items;

    // The current position of the stack
    struct history_item *ptr;

    // The oldest history item still remaining.  Increments after the base
    // pointer has wrapped around
    struct history_item *oldest_ptr;
};

extern struct history_stack *g_hist;

void history_init(void);
void history_deinit(void);
void history_push(struct uri *);

// History stack functions
const struct history_item *const history_pop(void);
const struct history_item *const history_next(void);

// History log functions
int history_log_display(void);

#endif
