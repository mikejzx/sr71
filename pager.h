#ifndef PAGER_H
#define PAGER_H

#include "gopher.h"
#include "typesetter.h"
#include "uri.h"
#include "search.h"

struct cached_item;

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

    // Whether this line is a heading
    // NOTE: if we have any more bools like this in future this should become
    // a bitfield enum
    bool is_heading;

    // Whether this line ends on a word hyphenated by the line breaking
    // algorithm (used for search functionality)
    bool is_hyphenated;

    // Line indent level
    int indent;

    // Length of prefix that is applied to this line
    size_t prefix_len;
};

struct pager_buffer
{
    char *b;
    size_t size;

    // Points where line-breaks occur in the buffer
    struct pager_buffer_line *lines;
    int line_count, lines_capacity;
};

struct pager_link
{
    struct uri uri;

    // Location/length of link in buffer
    char *buffer_loc;
    size_t buffer_loc_len;
    int line_index;
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
        int w, h;
        struct pager_buffer_line *rows;
        size_t rows_size;
    } visible_buffer, visible_buffer_prev;

    // Current pager scroll value
    int scroll;

    // List of links on the page
    struct pager_link *links;
    int link_count;
    int link_capacity;
    int selected_link_index, selected_link_index_prev;

    // Pager margins
    struct margin
    {
        int l, r;
    } margin;
    float margin_bias;

    // "Marks" (like in vi and less).  Only allow alphanumeric registers for
    // the moment to prevent any crazy issues.  If people seriously need to use
    // symbols as their registers then perhaps we could add them in?
    int marks['9' - '0' + 'Z' - 'A' +  'z' - 'a'];

    // If the buffer is from the cache; this is a pointer to the item itself
    struct cached_item *cached_page;

    // Searching
    struct search search;
};

extern struct pager_state *g_pager;

void pager_init(void);
void pager_load_buffer(struct pager_buffer *);
void pager_scroll(int);
void pager_scroll_topbot(int);
void pager_scroll_bottom(void);
void pager_scroll_paragraph(int);
void pager_scroll_heading(int);
void pager_paint(bool);
void pager_resized(void);
void pager_update_page(int, int);
void pager_select_first_link_visible(void);
void pager_select_last_link_visible(void);
void pager_check_link_capacity(void);

#endif
