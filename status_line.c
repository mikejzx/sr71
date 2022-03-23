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

        char text[64];
        int x_pos = 0;

        switch(cid)
        {
        case STATUS_LINE_COMPONENT_LEFT:
        {
            // Just display page URI for now
            c->bytes = uri_str(&g_state->uri,
                text, sizeof(text), URI_FLAGS_NONE);
            c->bytes = strnlen(text, sizeof(text));
            c->len = utf8_strnlen_w_formats(text, c->bytes);
            x_pos = 1;
        } break;

        case STATUS_LINE_COMPONENT_RIGHT:
        {
            int scroll_percent = (int)ceil(100.0 *
                g_pager->scroll / (g_pager->buffer.line_count - 1));
            switch (scroll_percent)
            {
            case 0:
                strcpy(text, "top");
                c->len = strlen("top");
                break;
            case 100:
                strcpy(text, "bottom");
                c->len = strlen("bottom");
                break;
            default:
                c->len = snprintf(text, sizeof(text), "%d%%", scroll_percent);
                break;
            }
            c->bytes = c->len;
            x_pos = g_tui->w - c->len + 1;
        } break;

        default: continue;
        }

        // Draw the new text
        tui_cursor_move(x_pos, g_tui->h - 1);
        tui_say("\x1b[30;43m");
        tui_sayn(text, c->bytes);
        tui_say("\x1b[0m");

        // Fill over previous text
        // This won't work with right-aligned component just yet though (TODO)
        if (cid == STATUS_LINE_COMPONENT_RIGHT)
        {
            tui_cursor_move(g_tui->w - (int)c->len_prev + 1, g_tui->h - 1);
        }
        else
        {
            tui_cursor_move(x_pos, g_tui->h - 1);
        }
        for (int j = c->len; j < c->len_prev; tui_say(" "), ++j);
    }
}
