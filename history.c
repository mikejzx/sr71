#include "pch.h"
#include "state.h"
#include "history.h"
#include "uri.h"

struct history_stack *g_hist;
static struct history_item *max_hist_ptr = NULL;

void
history_init(void)
{
    g_hist = &g_state->hist;
    g_hist->items = calloc(MAX_HISTORY_SIZE, sizeof(struct history_item));
    g_hist->ptr = g_hist->items;
    g_hist->oldest_ptr = g_hist->items;
    max_hist_ptr = g_hist->items + MAX_HISTORY_SIZE;
}

void
history_deinit(void)
{
    free(g_hist->items);
}

/* Return the theoretical next history item */
static struct history_item *
get_next_hist_ptr(void)
{
    if (g_hist->ptr + 1 >= max_hist_ptr)
    {
        return g_hist->items;
    }
    return g_hist->ptr + 1;
}

/* Return the theoretical previous history item */
static struct history_item *
get_prev_hist_ptr(void)
{
    if (g_hist->ptr - 1 < g_hist->items)
    {
        return max_hist_ptr - 1;
    }
    return g_hist->ptr - 1;
}

void
history_push(struct uri *uri, int last_sel, int last_scroll)
{
    // Wrap history back to start if needed
    g_hist->ptr = get_next_hist_ptr();

    // Increment oldest pointer
    if (g_hist->ptr == g_hist->oldest_ptr)
    {
        if (g_hist->oldest_ptr + 1 >= max_hist_ptr)
        {
            g_hist->oldest_ptr = g_hist->items;
        }
        else
        {
            ++g_hist->oldest_ptr;
        }
    }

    g_hist->ptr->initialised = true;
    g_hist->ptr->last_sel = last_sel;
    g_hist->ptr->last_scroll = last_scroll;
    memcpy(&g_hist->ptr->uri, uri, sizeof(struct uri));

    // Now that we've pushed new history onto the stack we need to deinitialise
    // everything else after what was here
    struct history_item *i;
    if (g_hist->ptr > g_hist->oldest_ptr)
    {
        for (i = g_hist->items;
            i < g_hist->oldest_ptr;
            i->initialised = false, ++i);
        for (i = g_hist->ptr + 1;
            i < max_hist_ptr;
            i->initialised = false, ++i);
    }
    else
    {
        for (i = g_hist->ptr + 1;
            i < g_hist->oldest_ptr;
            i->initialised = false, ++i);
    }
}

/* Go back in history */
const struct history_item *const
history_pop(void)
{
    struct history_item *prev = get_prev_hist_ptr();
    if (prev == g_hist->oldest_ptr ||
        !prev->initialised)
    {
        return NULL;
    }

    g_hist->ptr = prev;
    return g_hist->ptr;
}

/* Go forward in history */
const struct history_item *const
history_next(void)
{
    struct history_item *next = get_next_hist_ptr();
    if (next == g_hist->oldest_ptr ||
        !next->initialised)
    {
        return NULL;
    }

    g_hist->ptr = next;
    return g_hist->ptr;
}
