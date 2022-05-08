#ifndef PATHS_H
#define PATHS_H

/*
 * paths.h
 *
 * We dynamically allocate many of the program paths as they often involve
 * environment variables that need to be expanded.
 */

enum path_id
{
    PATH_ID_UNKNOWN = -1,

    PATH_ID_FAVOURITES,
    PATH_ID_HISTORY_LOG,
    PATH_ID_TOFU,

    PATH_ID_CACHE_ROOT,
    PATH_ID_CACHE_GEMINI,
    PATH_ID_CACHE_GOPHER,
    PATH_ID_CACHE_TMP,
    PATH_ID_CACHE_META,
    PATH_ID_CACHE_META_TMP,
    PATH_ID_CACHE_META_BAK,

    PATH_ID_COUNT
};

extern const char *path_ptrs[PATH_ID_COUNT];

static inline const char *
path_get(enum path_id id)
{
    if (id >= PATH_ID_COUNT || id < 0) return NULL;
    return path_ptrs[id];
}

int paths_init(void);
void paths_deinit(void);

#endif
