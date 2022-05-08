#include "pch.h"
#include "cache.h"
#include "history.h"
#include "paths.h"
#include "state.h"
#include "uri.h"

struct history_stack *g_hist;
static struct history_item *max_hist_ptr = NULL;
static struct history_item *get_next_hist_ptr(void);
static struct history_item *get_prev_hist_ptr(void);

void
history_init(void)
{
    // Initialise history stack
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

void
history_push(struct uri *uri)
{
    // Wrap undor/redo stack back to start if needed
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

    // Append the item to the history file
#if HISTORY_LOG_ENABLED
    // Don't log internal pages
    if (uri->protocol == PROTOCOL_INTERNAL) return;

    FILE *fp = fopen(path_get(PATH_ID_HISTORY_LOG), "a");
    if (!fp)
    {
        tui_status_say("error: failed to write to history log file");
        return;
    }

    // Write current timestamp, and URI string
    char uri_string[URI_STRING_MAX];
    size_t uri_string_len = uri_str(
        uri,
        uri_string,
        sizeof(uri_string), 0);
    if (uri_string_len)
    {
        fprintf(fp, "%lu %s\n", time(NULL), uri_string);
    }

    fclose(fp);
#endif
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

/* Format and display the history log file in the on-screen buffer */
int
history_log_display(void)
{
#if HISTORY_LOG_ENABLED
    tui_status_say("Reading history file ...");

    FILE *fp = fopen(path_get(PATH_ID_HISTORY_LOG), "rb");
    if (!fp)
    {
        tui_status_say("No history file yet");
        return -1;
    }

    // Seek to end of file
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        tui_status_say("error: failed to seek to end of history file");
        return -1;
    }

    // Set up the main header, etc.
    size_t buf_tmp_size = URI_STRING_MAX;
    char *buf_tmp = malloc(buf_tmp_size);
    size_t bytes = snprintf(buf_tmp, buf_tmp_size, "# Browsing history\n");
    recv_buffer_check_size(bytes);
    strncpy(g_recv->b, buf_tmp, bytes);
    g_recv->size = bytes;

    // Start headings on 'today'
    time_t now = time(NULL);
    int current_days, current_days_prev = -1;

    // Write the URIs in reverse to the recv buffer
    for (char buf[256]; getline_reverse(buf, sizeof(buf), fp) != NULL;)
    {
        // Find location of URI in buffer (after first space)
        const char *uri_start;
        for (uri_start = buf;
            uri_start < buf + sizeof(buf) && *uri_start != ' ';
            ++uri_start);

        // Parse item timestamp
        time_t ts = strtoul(buf, NULL, 10);

        // Print heading if timestamp difference in days changes
        current_days = timestamp_age_days_approx(ts, now);
        if (current_days > current_days_prev)
        {
            current_days_prev = current_days;

            if (current_days == 0)
            {
                bytes = snprintf(buf_tmp, buf_tmp_size, "\n## Today\n");
            }
            else if (current_days == 1)
            {
                bytes = snprintf(buf_tmp, buf_tmp_size, "\n## Yesterday\n");
            }
            else
            {
                bytes = snprintf(buf_tmp, buf_tmp_size,
                    "\n## %d days ago\n", current_days);
            }
            recv_buffer_check_size(g_recv->size + bytes);
            strncpy(g_recv->b + g_recv->size, buf_tmp, bytes);
            g_recv->size += bytes;
        }

        bytes = snprintf(buf_tmp, buf_tmp_size, "=> %s\n", uri_start);
        recv_buffer_check_size(g_recv->size + bytes);
        strncpy(g_recv->b + g_recv->size, buf_tmp, bytes);
        g_recv->size += bytes;
    }

    free(buf_tmp);
    fclose(fp);

    // Set MIME type to gemtext
    mime_parse(&g_recv->mime, MIME_GEMTEXT, strlen(MIME_GEMTEXT));

    tui_status_clear();
    return 0;
#else
    tui_status_say("History log feature not supported");
    return -1;
#endif
}
