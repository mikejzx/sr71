#ifndef STATUS_LINE_H
#define STATUS_LINE_H

#define STATUS_LINE_TOP_HEIGHT 0
#define STATUS_LINE_BOTTOM_HEIGHT 2

enum status_line_component_id
{
    STATUS_LINE_COMPONENT_LEFT = 0,
    STATUS_LINE_COMPONENT_RIGHT,

    STATUS_LINE_COMPONENT_COUNT
};

struct status_line_component
{
    bool invalidated;
    size_t len, bytes;
    size_t len_prev, bytes_prev;
};

struct status_line
{
    struct status_line_component components[STATUS_LINE_COMPONENT_COUNT];
};

extern struct status_line g_statline;

void status_line_init(void);
void status_line_paint(void);

#endif
