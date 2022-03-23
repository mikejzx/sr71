#ifndef PAGER_H
#define PAGER_H

#include "typesetter.h"
#include "uri.h"

struct pager_buffer_line
{
    // Pointer to where in buffer line begins
    const char *s;

    // Line length
    size_t len;

    // Line byte count
    size_t bytes;

    // The true 'raw' line index that this line exists under.  Used only for
    // the pager's processed/formatted buffer so we can track scroll position
    // on window size changes
    // This may be renamed to 'index' in future if other buffers require lines
    // to hold their own index for whatever reason.  Perhaps a union might even
    // work too.
    int raw_index;
    // And additionally the number of bytes this line is from the beginning of
    // the raw line
    size_t raw_dist;
};

struct pager_buffer
{
    char *b;
    size_t size;

    // Points where line-breaks occur in the buffer
    struct pager_buffer_line *lines;
    size_t line_count, lines_capacity;
};

struct pager_link
{
    struct uri uri;

    // Location/length of link in buffer
    char *buffer_loc;
    size_t buffer_loc_len;
    size_t line_index;
};

struct pager_state
{
    struct typesetter typeset;

    // Entire text buffer being displayed in this pager
    // It should be noted that this buffer is NOT a raw buffer (e.g. not a pure
    // gemtext buffer).  This is the buffer AFTER it has been processed and
    // typeset properly.  Therefore, this buffer gets updated each time the
    // window is resized, etc. to account for word wrapping.
    struct pager_buffer buffer;

    // The pager window's VISIBLE buffer.  This is what is actually painted.
    // Previous is kept to help with redrawing
    struct pager_visible_buffer
    {
        size_t w, h;
        struct pager_buffer_line *rows;
        size_t rows_size;
    } visible_buffer, visible_buffer_prev;

    // Current pager scroll value
    int scroll;

    // List of links on the page
    struct pager_link *links;
    size_t link_count;
    size_t link_capacity;
    int selected_link_index;
};

extern struct pager_state *g_pager;

void pager_init(void);
void pager_load_buffer(struct pager_buffer *);
void pager_scroll(int);
void pager_scroll_top(void);
void pager_scroll_bottom(void);
void pager_paint(void);
void pager_resized(void);
void pager_update_page(void);
void pager_select_first_link_visible(void);
void pager_select_last_link_visible(void);
void pager_check_link_capacity(void);

#endif
