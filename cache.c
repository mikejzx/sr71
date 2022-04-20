#include "pch.h"
#include "state.h"
#include "cache.h"
#include "tui.h"

#define DIR_PERMS (S_IRWXU | S_IRWXG)

static struct cache s_cache;

#if CACHE_USE_DISK
static const char
    *PATH_META = CACHE_PATH "/meta.dir",
    // Can't put on /tmp or else we get EXDEV errno
    *PATH_META_TMP = CACHE_PATH "/meta.dir.tmp",
    *PATH_META_BAK = CACHE_PATH "/meta.dir.bak";
enum
cache_meta_info_id
{
    CACHE_META_URI = 0,
    CACHE_META_SIZE,
    CACHE_META_MIME,
    CACHE_META_TIMESTAMP,
    CACHE_META_CHECKSUM,

    CACHE_META_COUNT
};

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

static const char *CACHE_PATHS[PROTOCOL_COUNT] =
{
    [PROTOCOL_UNKNOWN] = NULL,
    [PROTOCOL_GEMINI]  = CACHE_PATH_GEMINI,
    [PROTOCOL_GOPHER]  = CACHE_PATH_GOPHER,
    [PROTOCOL_FINGER]  = NULL,
    [PROTOCOL_FILE]    = NULL,
    [PROTOCOL_MAILTO]  = NULL,
};

/* Generate filepath for on-disk cache item */
static void
cache_gen_filepath(
    const struct uri *restrict const uri,
    char *restrict path,
    size_t path_size)
{
    const char *cachepath = CACHE_PATHS[uri->protocol];
    size_t len = snprintf(path, path_size,
        "%s/%s%s", cachepath, uri->hostname, uri->path);

    // If the resource is part of a directory, we need to manually add a
    // filename
    if (path[len - 1] == '/')
    {
        strncat(path, "index", path_size - len);
    }
    else
    {
        // Check if this resource name is already used by a directory; if so
        // then this needs to become an 'index' file.
        static struct stat path_stat;
        if (stat(path, &path_stat) < 0)
        {
            // Path doesn't exist; all fine
            return;
        }
        if (path_stat.st_mode & S_IFDIR)
        {
            // Make the path an index file
            strncat(path, "/index", path_size - len);
        }
    }
}

/*
 * Attempts to convert a on-disk cache file to a directory which can store
 * other cache files.
 *
 * This is useful in cases where we cache an item and wrongly assume it's not
 * the index of a directory.
 *  e.g. applied situation:
 *  + We visit gemini.circumlunar.space/docs, and hence create a cache file
 *    called 'docs' (technically doesn't happen here as the site redirects to
 *   'docs/', but we assume the server doesn't perform the redirect.
 *  + Then we visit gemini.circumlunar.space/docs/faq.gmi, and are now unable
 *    to cache the page faq.gmi because the on-disk 'docs' is not a directory
 *  + We attempt to remedy the issue by calling this function, which will
 *    rename the 'docs' file to 'index' under a newly-created 'docs/' directory
 * The function returns false if it fails to make the directory, and true on
 * success or if no action needed to be taken
 */
static bool
cache_file_to_dir(const char *fpath)
{
    // Check if the given path indeed exists on disk as a file
    static struct stat fpath_stat;
    if (stat(fpath, &fpath_stat) < 0)
    {
        // No such file on disk
        return false;
    }
    if (fpath_stat.st_mode & S_IFDIR)
    {
        // Path is already a directory; we don't need to do anything
        return true;
    }

    // Generate a path to temporarily move the file
    char path_tmp[] = CACHE_PATH "/tmp.XXXXXX",
         path_index[FILENAME_MAX];
    if (mkstemp(path_tmp) == -1) return false;

    // Rename file to a temporary file name
    if (rename(fpath, path_tmp) != 0) return false;

    // Make a directory where the file was
    if (mkdir(fpath, DIR_PERMS) != 0) return false;

    // Move the file to the new directory
    snprintf(path_index, sizeof(path_index), "%s/index", fpath);
    if (rename(path_tmp, path_index) != 0) return false;

    return true;
}

#endif // CACHE_USE_DISK

static struct cached_item *cache_get_next_item(void);

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
#if CACHE_USE_DISK
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
    int i;
#if CACHE_USE_DISK
    /*
     * Flush whatever cache is left in memory to the disk
     */

    FILE *fp, *fp_meta, *fp_meta_tmp;
    char path[FILENAME_MAX];

    /* Open the temporary metadata file for writing */
    if (!(fp_meta_tmp = fopen(PATH_META_TMP, "w"))) goto abort_flush;

    tui_status_say("Flushing cache to disk ...");

    /*
     * Iterate over cache items, write them to the disk and add to the metadata
     * state
     */
    for (i = 0; i < s_cache.count; ++i)
    {
        const struct cached_item *const item = &s_cache.items[i];

        // Make all the directories as needed
        cache_gen_filepath(&item->uri, path, sizeof(path));
        for (char *x = path; *x; ++x)
        {
            if (*x != '/') continue;
            *x = '\0';

            // Check if the path exists
            if (access(path, F_OK) != 0)
            {
                // Directory doesn't exist; so create it
                if (mkdir(path, DIR_PERMS) != 0)
                {
                    // Give up
                    goto fail;
                }
            }
            else
            {
                // The path exists, so we ensure it is indeed a *directory* and
                // not an already-stored file
                if (!cache_file_to_dir(path))
                {
                    // Give up
                    goto fail;
                }
            }

            *x = '/';
        }

        if (!(fp = fopen(path, "w"))) continue;

        // Write file to disk
        fwrite(item->data, item->data_size, 1, fp);
        fclose(fp);

        // Append to the temporary meta file
        fprintf(fp_meta_tmp,
            "%.*s"
            "\t%lu"
            "\t%s"
            "\t%lu"
            "\t",
            (int)item->uristr_len,
            item->uristr,
            item->data_size,
            item->mime.str,
            item->timestamp);
        for (unsigned h = 0; h < item->hash_len; ++h)
        {
            fprintf(fp_meta_tmp, "%02x", item->hash[h]);
        }
        fprintf(fp_meta_tmp, "\n");

    fail:
        continue;
    }

    // Now open the real meta file for reading, and read any URIs that haven't
    // already been written to the temporary one
    bool did_read_meta;
    if (access(PATH_META, F_OK) != 0 ||
        !(fp_meta = fopen(PATH_META, "r")))
    {
        did_read_meta = false;
        goto no_meta_file;
    }
    did_read_meta = true;

    size_t tmp;
    ssize_t len;
    for (char *line = NULL; (len = getline(&line, &tmp, fp_meta)) != -1;)
    {
        if (len < 1) continue;
        size_t uri_len = min(strcspn(line, "\t\n"), len - 1);

        // Check if this URI already has been written
        for (i = 0; i < s_cache.count; ++i)
        {
            const struct cached_item *const item = &s_cache.items[i];

            if (uri_len == item->uristr_len &&
                strncmp(item->uristr, line, uri_len) == 0)
            {
                // Don't add this URI as we already have it
                goto skip;
            }
        }

        // Add the line to the temporary meta file
        (void)!fwrite(line, len, 1, fp_meta_tmp);

    skip:;
    }

    fclose(fp_meta);
no_meta_file:
    fclose(fp_meta_tmp);

    /* Now replace the original metadata file with the temporary one */

    if (did_read_meta)
    {
        remove(PATH_META_BAK);
        // Make backup of meta.dir
        if (rename(PATH_META, PATH_META_BAK) != 0)
        {
            goto abort_flush;
        }
    }
    // Make temp file the main one
    if (rename(PATH_META_TMP, PATH_META) != 0)
    {
        goto abort_flush;
    }
abort_flush:

#endif // CACHE_USE_DISK

    /* Free everything */
    for (i = 0; i < s_cache.count; ++i)
    {
        const struct cached_item *const item = &s_cache.items[i];
        free(item->data);
    }

    // Deallocate
    free(s_cache.items);
}

/* Find a URI in the cache */
bool
cache_find(
    const struct uri *restrict const uri,
    struct cached_item **restrict const o)
{
    g_recv->b_alt = NULL;

    // For now we don't cache pages that have a URI query
    if (*uri->query) return false;

    // See if we have the URI cached in memory already
    for (int i = 0; i < s_cache.count; ++i)
    {
        struct cached_item *item = &s_cache.items[i];
        if (uri_cmp_notrailing(&item->uri, uri) != 0) continue;

        // Page is cached; set it as the alternative recv buffer (instead of
        // memcpy'ing directly into the recv buffer)
        g_recv->size = item->data_size;
        g_recv->b_alt = item->data;
        g_recv->mime = item->mime;

        *o = item;
        return true;
    }

#if CACHE_USE_DISK
    // Search the on-disk cache
    static char path[FILENAME_MAX];
    FILE *fp;

    // Check if the file exists on the disk
    cache_gen_filepath(uri, path, sizeof(path));
    if (access(path, F_OK) != 0)
    {
        return false;
    }

    // Generate URI string
    char uri_string[URI_STRING_MAX];
    size_t uri_string_len = uri_str(
        uri,
        uri_string,
        sizeof(uri_string),
        URI_FLAGS_NO_PORT_BIT |
            URI_FLAGS_NO_TRAILING_SLASH_BIT |
            URI_FLAGS_NO_GOPHER_ITEM_BIT |
            URI_FLAGS_NO_QUERY_BIT);

    /* Read the metadata file */
    if (!(fp = fopen(PATH_META, "r")))
    {
        return false;
    }

    // Item which will be added to memory pretty soon
    struct cached_item item;
    item.data_size = 0;
    item.session.last_sel = -1;
    item.session.last_scroll = 0;

    tui_status_say("Checking disk cache ...");

    // Iterate over metadata file's lines
    size_t tmp;
    ssize_t n_bytes;
    bool read = false;
    for (char *line = NULL; (n_bytes = getline(&line, &tmp, fp)) != -1;)
    {
        if (n_bytes < 1) continue;
        size_t uri_len = min(strcspn(line, "\t\n"), n_bytes - 1);

        // Check if URIs match
        if (uri_len != uri_string_len ||
            strncmp(uri_string, line, uri_string_len) != 0) continue;

        enum cache_meta_info_id meta_id = CACHE_META_URI + 1;

        // Copy URI
        item.uri = uri_parse(uri_string, uri_len);
        item.uristr_len = uri_len;
        strncpy(item.uristr, uri_string, uri_len);

        // Parse file metadata
        char *m_last = line + uri_len;
        for (char *m = m_last + 1;;
            ++m)
        {
            if (*m && *m != '\t' && *m != '\n') continue;

            switch(meta_id)
            {
            case CACHE_META_SIZE:
                item.data_size = strtoul(m_last, NULL, 10);
                break;
            case CACHE_META_MIME:
                mime_parse(&item.mime, m_last, m - m_last);
                break;
            case CACHE_META_TIMESTAMP:
                item.timestamp = strtoul(m_last, NULL, 10);
                break;
            case CACHE_META_CHECKSUM:
                item.hash_len = 0;
                for (char *x = m_last;
                    x < m;
                    x += 2)
                {
                    // I swear why can't there just be a strtol that accepts
                    // max bytes or something...
                    char *term = x + 2;
                    char term_old;
                    if (term < m)
                    {
                        term_old = *term;
                        *term = '\0';
                    }

                    item.hash[item.hash_len++] = strtol(x, NULL, 16);

                    if (term < m) *term = term_old;
                }
                break;
            default: break;
            }

            // End of line
            if (!*m || m >= line + n_bytes) break;

            m_last = m + 1;
            if (meta_id + 1 >= CACHE_META_COUNT) break;
            ++meta_id;
        }

        read = true;
        break;
    }

    fclose(fp);

    if (!read) goto fail;

    /* Read the file content from the disk */
    fp = fopen(path, "r");

    item.data = malloc(item.data_size);

    // Read file
    bool success = fread(item.data, item.data_size, 1, fp) > 0;
    fclose(fp);

    if (!success) goto fail;

    // Copy to the actual pager buffer
    g_recv->size = item.data_size;
    g_recv->b_alt = item.data;
    g_recv->mime = item.mime;

    struct cached_item *item_real = cache_get_next_item();
    if (!item_real)
    {
        free(item.data);
        return false;
    }
    *item_real = item;
    *o = item_real;
    return true;
#endif // CACHE_USE_DISK

fail:
    g_recv->size = 0;
    return false;
}

/* Push current page to cache */
struct cached_item *
cache_push_current(void)
{
    struct cached_item *item = NULL;

    if (
        // For now we don't cache pages that have a URI query
        *g_state->uri.query ||
        // Don't cache internal pages
        g_state->uri.protocol == PROTOCOL_INTERNAL) return NULL;

    // Check if the URI is in the cache already; so we can update it
    for (int i = 0; i < s_cache.count; ++i)
    {
        struct cached_item *m = &s_cache.items[i];
        if (uri_cmp_notrailing(&m->uri, &g_state->uri) != 0) continue;

        item = m;
        s_cache.total_size -= m->data_size;
        free(item->data);
        break;
    }

    if (!item) { item = cache_get_next_item(); }

    if (!item)
    {
        tui_status_begin();
        tui_say("cache: max size of ");
        tui_print_size(CACHE_IN_MEM_MAX_SIZE);
        tui_say(" exceeded.");
        tui_status_end();
        return NULL;
    }

    item->uri = g_state->uri;
    item->timestamp = time(NULL);
    item->mime = g_recv->mime;
    item->data_size = g_recv->size;
    item->data = malloc(item->data_size);
    item->session.last_sel = -1;
    item->session.last_scroll = 0;
    memcpy(item->data, g_recv->b, item->data_size);

#if CACHE_USE_DISK
    // Write URI string
    item->uristr_len = uri_str(
        &item->uri,
        item->uristr,
        sizeof(item->uristr),
        URI_FLAGS_NO_PORT_BIT |
            URI_FLAGS_NO_TRAILING_SLASH_BIT |
            URI_FLAGS_NO_GOPHER_ITEM_BIT |
            URI_FLAGS_NO_QUERY_BIT);

    // Generate a SHA256 hash of the content.  The algorithm used shouldn't
    // matter too much, as it's literally only used to detect changes in file
    // content.
    const EVP_MD *md = EVP_sha256();
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, md, NULL);
    EVP_DigestUpdate(ctx, item->data, item->data_size);
    EVP_DigestFinal_ex(ctx, item->hash, &item->hash_len);
    EVP_MD_CTX_free(ctx);
#endif // CACHE_USE_DISK

    s_cache.total_size += item->data_size;

    return item;
}

static struct cached_item *
cache_get_next_item(void)
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
            free(item->data);
            s_cache.total_size -= item->data_size;
        }
        return item;
    }

    // Add a new item
    if (s_cache.count + 1 >= s_cache.capacity)
    {
        // TODO: reallocate
        return NULL;
    }
    return &s_cache.items[s_cache.count++];
}
