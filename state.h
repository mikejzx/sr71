#ifndef STATE_H
#define STATE_H

#include "tui.h"
#include "pager.h"
#include "gemini.h"
//#include "gopher.h"

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
        char *b;

        // Size of content in the buffer
        size_t size;

        // Maximum size of content until realloc needed
        size_t capacity;
    } recv_buffer;

    // Client states
    struct gemini gem;
    //struct gopher ph;

    // List of open tabs
    // struct page_tab *tabs;
    // int tab_count;
    // int tab_capacity;
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
