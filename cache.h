#ifndef CACHE_H
#define CACHE_H

#include "uri.h"
#include "mime.h"

/*
 * cache.h
 *
 * Document caching system.
 *
 * The caching system should have the following properties:
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
 * + Cached resources are timestamped, though the timestamp serves pretty much
 *   no purpose other than for display reasons.  Perhaps items which exceed a
 *   threshold could invoke a new request to the server.
 * + A simple metadata file (meta.dir) is stored in the cache directory and
 *   keeps info about each of the cached items (i.e. timestamp, checksum, MIME
 *   type, etc.)
 * + Perhaps "versions" of cached items can be kept?
 * + May possibly look into compression of local cache? (lz4 or gzip should do)
 */

// Indicate that we want to build with persistent disk caching enabled
#define CACHE_USE_DISK

#define CACHE_ITEM_CAPACITY_INITIAL (128)

// Allow 128 MiB of in-memory cache
#define CACHE_IN_MEM_MAX_SIZE (1024 * 1024 * 128)

// Represents and in-memory cached item
struct cached_item
{
    // The URI of the cached item
    struct uri uri;

#ifdef CACHE_USE_DISK
    // A string of the URI, so we don't have to keep re-writing it (only used
    // for disk cache at the moment)
    char uristr[URI_STRING_MAX];
    size_t uristr_len;

    // Checksum for checking for content changes
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned hash_len;
#endif

    // UNIX timestamp of when the item was pushed to cache
    time_t timestamp;

    // MIME type of the page
    struct mime mime;

    // Item data
    char *data;
    size_t data_size;

    // This info is only kept for current session
    struct cached_item_session_info
    {
        // The last-selected link on this page (used for restoring it when
        // navigating through history)
        int last_sel;

        // Last scroll position on the page
        int last_scroll;
    } session;
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
struct cached_item *cache_push_current(void);
bool cache_find(
    const struct uri *restrict const,
    struct cached_item **restrict const);

#endif
