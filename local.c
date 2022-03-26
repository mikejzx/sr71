#include "pch.h"
#include "state.h"
#include "local.h"
#include "uri.h"
#include "tui.h"

int
local_request(struct uri *uri, int *n_dirents)
{
    // Get file path
    static char path[FILENAME_MAX];
    uri_str(uri, path, sizeof(path), URI_FLAGS_NO_PROTOCOL_BIT);

    // Check what type of file the URI is
    struct stat path_stat;
    if (stat(path, &path_stat) < 0)
    {
        tui_cmd_status_prepare();
        tui_say("No such file or directory");
        return -1;
    }

    bool is_dir = path_stat.st_mode & S_IFDIR;
    *n_dirents = 0;

    if (!is_dir)
    {
        // Regular file
        FILE *file = fopen(path, "r");
        if (!file)
        {
            tui_cmd_status_prepare();
            tui_say("Failed to open local file");
            return -1;
        }

        tui_cmd_status_prepare();
        tui_printf("Loading local file %s", path);

        // Read size of file and resize buffer if needed
        fseek(file, 0, SEEK_END);
        size_t len = ftell(file);
        recv_buffer_check_size(len);

        // Read file
        fseek(file, 0, SEEK_SET);
        bool success = fread(g_recv->b, len, 1, file) > 0;
        fclose(file);

        if (!success)
        {
            return -1;
        }

        // Set MIME type to gemtext for now
        // TODO: determine file mimetypes
        mime_parse(&g_recv->mime, MIME_GEMTEXT, strlen(MIME_GEMTEXT));
    }
    else
    {
        // Read directory and create listing
        DIR *dir = opendir(path);
        if (!dir)
        {
            tui_cmd_status_prepare();
            tui_say("failed to open directory");
            return -1;
        }
        size_t max_len = FILENAME_MAX + 128;
        char *buf_tmp = malloc(max_len);
        size_t bytes;

        bytes = snprintf(buf_tmp, max_len,
            "# Index of %s\n"
            "\n"
            "=> ..\n",
            path);
        recv_buffer_check_size(bytes);
        strncpy(g_recv->b, buf_tmp, bytes);
        g_recv->size = bytes;

        // Iterate over the directory and generate a gemtext file of the
        // directory listing
        *n_dirents = 1;
        for (struct dirent *entry = readdir(dir);
            entry;
            entry = readdir(dir))
        {
            // Skip '.' and '..'; we add '..' manually so it's always at top
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) continue;

            bytes = snprintf(buf_tmp, max_len,
                "=> %s\n",
                entry->d_name);

            recv_buffer_check_size(g_recv->size + bytes);
            strncpy(g_recv->b + g_recv->size, buf_tmp, bytes);
            g_recv->size += bytes;

            ++*n_dirents;
        }

        (void)closedir(dir);
        free(buf_tmp);

        // Set MIME type to gemtext (as our directory listing is in gemtext)
        mime_parse(&g_recv->mime, MIME_GEMTEXT, strlen(MIME_GEMTEXT));
    }

    return 0;
}
