#ifndef STATE_H
#define STATE_H

#include "gemini.h"
#include "gopher.h"
#include "history.h"
#include "mime.h"
#include "pager.h"
#include "tui.h"

struct state
{
    // Text interface state
    struct tui_state tui;

    // Pager state
    struct pager_state pager;

    // Current URI we are at
    struct uri uri;

    // Raw buffer which data is received into
    struct recv_buffer
    {
        // This is the main content buffer used for receiving new data
        char *b;

        // Maximum size of content until realloc of our buffer is needed
        size_t capacity;

        // This is a pointer to an "alternative" buffer.  This can point to
        // memory that is NOT owned by this actual struct.  If this is not
        // null, this will be the buffer that is used.
        // E.g. often used to point to cached item's buffers, as there's no
        //      need to memcpy their contents into the actual recv buffer
        const char *b_alt;

        // Size of whatever content is in either buffer
        size_t size;

        // Buffer MIME type
        struct mime mime;
    } recv_buffer;

    // Client states
    struct gemini gem;
    struct gopher ph;

    // History stack (for undo/redo)
    struct history_stack hist;
};

extern struct state *g_state;
extern struct recv_buffer *g_recv;

/* Ensure recv buffer has enough size, and if not then reallocate it */
static inline void
recv_buffer_check_size(size_t len)
{
    // All good
    if (g_recv->capacity >= len) return;

    // Perform reallocate
    size_t new_size = (len * 3) / 2;
    void *tmp = realloc(g_recv->b, new_size);
    if (!tmp)
    {
        fprintf(stderr, "fatal: out of memory!\n");
        exit(-1);
    }
    g_recv->b = tmp;
    g_recv->capacity = new_size;
}

#endif
