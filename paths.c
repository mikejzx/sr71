#include "pch.h"
#include "paths.h"

// We need to describe the paths in some way to indicate which are local data
// directories, which are not, etc.
static const struct path_info
{
    union
    {
        bool is_valid;

        enum path_info_prefix
        {
            // Use to null-terminate the array (sets is_valid to false)
            PATH_PREFIX_NULL = 0,

            // ~/.local/share
            PATH_PREFIX_DATA,

            // NOTE: when adding other path types here, they need to be
            //       implemented properly (need correct string copied and sizes
            //       adjusted) as at the moment on the data directory is
            //       implemented.
        } prefix;
    };

    // The path suffix itself
    const char *const suffix;
} PATH_INFOS[PATH_ID_COUNT + 1] =
{
    [PATH_ID_FAVOURITES]     = { { PATH_PREFIX_DATA }, "/favourites"         },
    [PATH_ID_HISTORY_LOG]    = { { PATH_PREFIX_DATA }, "/history.log"        },
    [PATH_ID_TOFU]           = { { PATH_PREFIX_DATA }, "/trusted_hosts"      },

    [PATH_ID_CACHE_ROOT]     = { { PATH_PREFIX_DATA }, "/cache"              },
    [PATH_ID_CACHE_GEMINI]   = { { PATH_PREFIX_DATA }, "/cache/gemini"       },
    [PATH_ID_CACHE_GOPHER]   = { { PATH_PREFIX_DATA }, "/cache/gopher"       },
    // Can't put temporary on /tmp or else we get EXDEV errno...
    [PATH_ID_CACHE_TMP]      = { { PATH_PREFIX_DATA }, "/cache/tmp.XXXXXX"   },
    [PATH_ID_CACHE_META]     = { { PATH_PREFIX_DATA }, "/cache/meta.dir"     },
    [PATH_ID_CACHE_META_TMP] = { { PATH_PREFIX_DATA }, "/cache/meta.dir.tmp" },
    [PATH_ID_CACHE_META_BAK] = { { PATH_PREFIX_DATA }, "/cache/meta.dir.bak" },

    [PATH_ID_COUNT]          = { { 0 } }
};

// All paths are stored in a single contiguous block of memory
static char *s_paths;

// Stores pointers to the paths buffer foreach id.
const char *path_ptrs[PATH_ID_COUNT] = { 0 };

int
paths_init(void)
{
    /* Get the user's data directory */
    char data_path[FILENAME_MAX];

    // First check if there's a special overriden path via environment variable
    if (getenv("SR71_DATA_DIR"))
    {
        strncpy(data_path, getenv("SR71_DATA_DIR"), sizeof(data_path));
        goto got_path;
    }

    const char *env_xdg = getenv("XDG_DATA_HOME");
    if (env_xdg)
    {
        // If they define XDG_DATA_HOME then we use that as the base
        snprintf(data_path, sizeof(data_path),
            "%s/" PROGRAM_NAME, env_xdg);
    }
    else
    {
        // Or else we just set it to ~/.local/share/
        const char *env_home = getenv("HOME");
        if (!env_home)
        {
            fprintf(stderr, "$HOME environment variable not defined!  "
                "Cannot initialise data directory path.\n"
                "This variable should be set to your user home directory.\n");
            return -1;
        }
        snprintf(data_path, sizeof(data_path),
            "%s/.local/share/" PROGRAM_NAME, env_home);
    }
got_path:

    // Create the data directory if it doesn't exist.
    if (access(data_path, F_OK) != 0 &&
        mkdir(data_path, S_IRWXU | S_IRWXG) != 0)
    {
        fprintf(stderr, "Failed to create non-existent data path '%s'.\n",
            data_path);
        return -1;
    }

    int data_path_len = strlen(data_path);

    const struct path_info *info;

    /* First calculate size of paths buffer */
    ssize_t total_len = 0;
    for (info = PATH_INFOS;
        (*info).is_valid;
        total_len += data_path_len + strlen(info->suffix) + 1, ++info);

    /* Allocate the buffer */
    s_paths = malloc(total_len);

    /* Write paths to the buffer */
    char *pos = s_paths;
    const char **path_ptr = path_ptrs;
    for (info = PATH_INFOS; (*info).is_valid; ++info, ++path_ptr)
    {
        *path_ptr = pos;
        strcpy(pos, data_path);
        pos += data_path_len;
        strcpy(pos, info->suffix);
        pos += strlen(info->suffix);
        *pos = '\0';
        ++pos;
    }

    return 0;
}

void
paths_deinit(void)
{
    free(s_paths);
}
