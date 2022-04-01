#include "pch.h"
#include "state.h"
#include "status_line.h"
#include "tui.h"
#include "pager.h"

struct status_line g_statline;

void
status_line_init(void)
{
    for (int cid = 0; cid < STATUS_LINE_COMPONENT_COUNT; ++cid)
    {
        struct status_line_component *const c = &g_statline.components[cid];
        c->invalidated = true;
    }
}

void
status_line_paint(void)
{
    // Draw status line
    for (int cid = 0; cid < STATUS_LINE_COMPONENT_COUNT; ++cid)
    {
        struct status_line_component *const c = &g_statline.components[cid];

        // Don't need to update it
        if (!c->invalidated) continue;

        c->len_prev = c->len;

        char text[256];
        int x_pos = 0;

        switch(cid)
        {
        case STATUS_LINE_COMPONENT_LEFT:
        {
            // Just display page URI for now
            c->bytes = uri_str(&g_state->uri,
                text, sizeof(text), URI_FLAGS_FANCY_BIT);
            c->len = utf8_strnlen_w_formats(text, c->bytes);

            // Don't render whole text if it exceeds terminal width
            if (c->len > g_tui->w - 4)
            {
                c->len = g_tui->w - 4;
                c->bytes = utf8_size_w_formats(text, c->len);
            }

            x_pos = 1;
        } break;

        case STATUS_LINE_COMPONENT_RIGHT:
        {
            int scroll_percent = (int)ceil(100.0 *
                g_pager->scroll /
                ((int)max(g_pager->buffer.line_count, 2) - 1));
            scroll_percent = max(scroll_percent, 0);
            switch (scroll_percent)
            {
            case 0:
                c->len = snprintf(text, sizeof(text),
                    "%s  top", g_recv->mime.str);
                break;
            case 100:
                c->len = snprintf(text, sizeof(text),
                    "%s  bottom", g_recv->mime.str);
                break;
            default:
                c->len = snprintf(text, sizeof(text),
                    "%s  %d%%", g_recv->mime.str, scroll_percent);
                break;
            }
            c->bytes = c->len;
            x_pos = g_tui->w - c->len + 1;
        } break;

        default: continue;
        }

        // Draw the new text
        tui_cursor_move(x_pos, g_tui->h - 1);
        //tui_say("\x1b[30;42m");
        tui_say("\x1b[2m");
        tui_sayn(text, c->bytes);
        tui_say("\x1b[0m");

        // Clear previous text
        int clear = (int)(c->len_prev - c->len);
        if (clear <= 0) continue;
        if (cid == STATUS_LINE_COMPONENT_RIGHT)
        {
            tui_cursor_move(
                max(g_tui->w - (int)c->len_prev + 1, 0),
                g_tui->h - 1);
        }
        tui_printf("%*s", clear, "");
    }
}
