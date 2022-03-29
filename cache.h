#ifndef CACHE_H
#define CACHE_H

#include "uri.h"
#include "mime.h"

/*
 * cache.h
 *
 * Document caching system.
 *
 * The caching system has the following properties:
 * + Pages browsed during the current session are kept in memory.  Once the
 *   limit is reached for cached pages, old pages are written to disk.
 * + Pages which are still in memory are written to disk on exit.
 * + The on-disk cache is in a simple file structure as you'd expect it to
 *   appear in, e.g.:
 *      cache/gemini/gemini.circumlunar.space/index.gmi
 *      cache/gemini/gemini.circumlunar.space/docs/index.gmi
 *      cache/gemini/gemini.circumlunar.space/docs/faq.gmi
 *      cache/gopher/gopher.floodgap.com/index
 *     etc.
 * + When about to visit a URI, we first check our in-memory cache for the link
 *   and load it if it exists.  If it doesn't then we check from disk.  Finally
 *   we download the resource over the Internet if we haven't it cached.
 * + Cached resources are timestamped, and their age is checked before
 *   retrieving.  If their age exceeds a certain threshold then we attempt to
 *   reload the page; the original cached item is kept if the reload fails
 *   (e.g. if the page is removed)
 * + A single metadata file is kept with each cached resource on disk, to keep
 *   the stored timestamp of when it was cached (and perhaps a checksum)
 * + Perhaps "versions" of cached items can be kept.
 * + May possibly look into compression of local cache?
 */

// Indicate that we want to build with persistent disk caching enabled
//#define CACHE_USE_DISK

#define CACHE_ITEM_CAPACITY_INITIAL 128

// Allow 128 MiB of in-memory cache
#define CACHE_IN_MEM_MAX_SIZE (1024 * 1024 * 128)
//#define CACHE_IN_MEM_MAX_SIZE (4096 * 32)

// Maximum age of a cached resource (seconds) before fetching latest version
//#define CACHE_MAX_AGE (60 * 60 * 24)

// Represents and in-memory cached item
struct cached_item
{
    // The URI of the cached item
    struct uri uri;

    // UNIX timestamp of when the item was pushed to cache
    time_t timestamp;

    // MIME type of the page
    struct mime mime;

    // Item data
    char *data;
    size_t data_size;
};

struct cache
{
    // The current in-memory cache
    size_t capacity;
    struct cached_item *items;
    size_t count;

    // Total size of all cached item in memory
    size_t total_size;
};

int cache_init(void);
void cache_deinit(void);
void cache_push_current(void);
bool cache_find(struct uri *);

#endif
