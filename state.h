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

    // Client states
    struct gemini gem;
    //struct gopher ph;

    // List of open tabs
    // struct page_tab *tabs;
    // int tab_count;
    // int tab_capacity;
};

extern struct state *g_state;

#endif
