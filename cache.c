#include "pch.h"
#include "state.h"
#include "cache.h"
#include "tui.h"

#define DIR_PERMS (S_IRWXU | S_IRWXG)

static struct cache s_cache;

#ifdef CACHE_USE_DISK
static int
cache_create_path(const char *p) { return mkdir(p, DIR_PERMS); }

static int
cache_create_gem(void)
{
    if (access(CACHE_PATH_GEMINI, F_OK) == 0) return 0;
    return cache_create_path(CACHE_PATH_GEMINI);
}

static int
cache_create_ph(void)
{
    if (access(CACHE_PATH_GOPHER, F_OK) == 0) return 0;
    return cache_create_path(CACHE_PATH_GOPHER);
}
#endif // CACHE_USE_DISK

int
cache_init(void)
{
    // Allocate in-memory cache
    s_cache.capacity = CACHE_ITEM_CAPACITY_INITIAL;
    s_cache.items = malloc(
        sizeof(struct cached_item) * s_cache.capacity);
    s_cache.count = 0;
    s_cache.total_size = 0;

    // If cache directory on disk doesn't exist, create it
#ifdef CACHE_USE_DISK
    if (access(CACHE_PATH, F_OK) != 0 &&
        // Create with RW for owner and group
        mkdir(CACHE_PATH, DIR_PERMS) != 0)
    {
        tui_status_say("cache: failed to make cache directory " CACHE_PATH);
        return -1;
    }

    if (cache_create_gem() != 0) return -1;
    if (cache_create_ph() != 0) return -1;
#endif // CACHE_USE_DISK

    return 0;
}

void
cache_deinit(void)
{
#ifdef CACHE_USE_DISK
    // Flush whatever cache is left in memory to the disk
    char path[FILENAME_MAX];
    FILE *fp;
    for (int i = 0; i < s_cache.count; ++i)
    {
        const struct cached_item *const item = &s_cache.items[i];

        const char *cachepath;
        if (item->uri.protocol == PROTOCOL_GEMINI)
        {
            cachepath = CACHE_PATH_GEMINI;
        }
        else if (item->uri.protocol == PROTOCOL_GOPHER)
        {
            cachepath = CACHE_PATH_GOPHER;
        }
        snprintf(path, sizeof(path),
            "%s/%s/%s", cachepath, item->uri.hostname, item->uri.path);

        // Make all the directories as needed
        for (char *x = path; *x; ++x)
        {
            if (*x != '/') continue;
            char c = *x;
            *x = '\0';

            if (access(path, F_OK) != 0)
            {
                // Make the directory
                if (mkdir(path, DIR_PERMS) != 0)
                {
                    // Give up
                    goto fail;
                }
            }
            *x = c;
        }

        if (!(fp = fopen(path, "w")))
        {
            continue;
        }

        // Write file to disk
        fwrite(item->data, item->data_size, 1, fp);

        fclose(fp);

    fail:
        continue;
    }
#endif // CACHE_USE_DISK

    for (int i = 0; i < s_cache.count; ++i)
    {
        const struct cached_item *const item = &s_cache.items[i];
        free(item->data);
    }

    // Deallocate
    free(s_cache.items);
}

/* Find a URI in the cache */
bool
cache_find(const struct uri *const uri)
{
    // See if we have the URI cached in memory already
    for (int i = 0; i < s_cache.count; ++i)
    {
        struct cached_item *item = &s_cache.items[i];
        if (uri_cmp(&item->uri, uri) != 0) continue;

        // Page is cached; load it into the recv buffer.
        recv_buffer_check_size(item->data_size);
        g_recv->size = item->data_size;
        memcpy(g_recv->b, item->data, g_recv->size);
        g_recv->mime = item->mime;

        return true;
    }

#ifdef CACHE_USE_DISK
    // Search the on-disk cache
#endif

    return false;
}

/* Push current page to cache */
void
cache_push_current(void)
{
    struct cached_item *item = NULL;

    if (s_cache.total_size + g_recv->size > CACHE_IN_MEM_MAX_SIZE &&
        s_cache.count)
    {
        // Replace oldest cached item
        time_t oldest = s_cache.items[0].timestamp;
        for (int i = 0; i < s_cache.count; ++i)
        {
            if (s_cache.items[i].timestamp < oldest)
            {
                item = &s_cache.items[i];
                oldest = item->timestamp;
            }
        }

        if (item)
        {
            s_cache.total_size -= item->data_size;
        }
    }
    else
    {
        // Add a new item
        if (s_cache.count + 1 >= s_cache.capacity)
        {
            // TODO: reallocate
            return;
        }
        item = &s_cache.items[s_cache.count++];
    }

    if (!item)
    {
        tui_status_begin();
        tui_say("cache: max size of ");
        tui_print_size(CACHE_IN_MEM_MAX_SIZE);
        tui_say(" exceeded.");
        tui_status_end();
        return;
    }

    item->uri = g_state->uri;
    item->timestamp = time(NULL);
    item->mime = g_recv->mime;
    item->data_size = g_recv->size;
    item->data = malloc(item->data_size);
    memcpy(item->data, g_recv->b, item->data_size);

    s_cache.total_size += item->data_size;
}
