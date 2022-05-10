#include "pch.h"
#include "favourites.h"
#include "paths.h"
#include "state.h"
#include "tui.h"
#include "uri.h"

static struct fav_node *s_head, *s_tail;
static bool s_favs_modified = false;

void
favourites_init(void)
{
    /*
     * Read the favourites file into a linked-list
     */
    s_head = NULL, s_tail = NULL;

    FILE *fp;

    // Create file if it doesn't exist
    if (access(path_get(PATH_ID_FAVOURITES), F_OK) != 0)
    {
        fp = fopen(path_get(PATH_ID_FAVOURITES), "w");
        fclose(fp);

        // No favourites, leave the list empty
        return;
    }

    // Open for reading
    fp = fopen(path_get(PATH_ID_FAVOURITES), "r");
    if (!fp)
    {
        tui_status_say("error: failed to read favourites file");
        return;
    }

    // Read the file line-by-line
    ssize_t len;
    size_t len_tmp;
    struct fav_node *n;
    char *line;
    for (line = NULL; (len = getline(&line, &len_tmp, fp)) != -1;)
    {
        // First column (to whitespace) is the URI itself
        // Everything after that is the favourite name
        const char *uri_start = line;
        const char *uri_end = uri_start + strcspn(uri_start, " \n");
        const char *title_start = uri_end + 1;
        const char *title_end = title_start + strcspn(title_start, "\n");

        // Create the node
        n = calloc(1, sizeof(struct fav_node));
        if ((int)(title_end - title_start) > 0)
        {
            strncpy(n->title,
                title_start,
                min(FAVOURITE_TITLE_MAX, title_end - title_start));
        }
        else
        {
            *n->title = '\0';
        }
        int uri_len = (int)(uri_end - uri_start);
        n->uri = malloc(uri_len + 1);
        strncpy(n->uri, uri_start, uri_len);
        n->uri[uri_len] = '\0';

        // Push the node
        favourites_push(n);
    }
    if (line) free(line);

    s_favs_modified = false;

    fclose(fp);
}

void
favourites_deinit(void)
{
    /*
     * Write the favourites file if we modified favourites list
     * */
    if (!s_favs_modified) goto cleanup;

    FILE *fp = fopen(path_get(PATH_ID_FAVOURITES), "w");
    if (!fp) goto cleanup;

    for (struct fav_node *n = s_head; n != NULL; n = n->link_n)
    {
        fprintf(fp, "%s %s\n", n->uri, n->title);
    }
    fclose(fp);

cleanup:
    /*
     * Free the favourites linked list
     */
    for (struct fav_node *n = s_head;
        n != NULL;)
    {
        struct fav_node *tmp = n->link_n;
        free(n->uri);
        free(n);
        n = tmp;
    }
}

/* Displays user favourited pages in a buffer */
int
favourites_display(void)
{
    // Set up header, etc.
    size_t buf_tmp_size = URI_STRING_MAX;
    char *buf_tmp = malloc(buf_tmp_size);
    size_t bytes = snprintf(buf_tmp, buf_tmp_size, "# Favourite pages\n\n");
    recv_buffer_check_size(bytes);
    strncpy(g_recv->b, buf_tmp, bytes);
    g_recv->size = bytes;

    if (!s_head)
    {
    #define NO_FAVOURITES_MESSAGE "You have no favourite pages."
    #define NO_FAVOURITES_MESSAGE_LEN (strlen(NO_FAVOURITES_MESSAGE) + 1)
        recv_buffer_check_size(g_recv->size + NO_FAVOURITES_MESSAGE_LEN);
        strncpy(g_recv->b + g_recv->size,
            NO_FAVOURITES_MESSAGE,
            NO_FAVOURITES_MESSAGE_LEN);
        g_recv->size += NO_FAVOURITES_MESSAGE_LEN;
    }
    else
    {
        // Read each of the favourites from the list
        for (struct fav_node *n = s_head; n != NULL; n = n->link_n)
        {
            bytes = snprintf(buf_tmp, buf_tmp_size,
                "=> %s %s\n",
                n->uri,
                n->title);
            recv_buffer_check_size(g_recv->size + bytes);
            strncpy(g_recv->b + g_recv->size, buf_tmp, bytes);
            g_recv->size += bytes;
        }
    }
    free(buf_tmp);

    mime_parse(&g_recv->mime, MIME_GEMTEXT, strlen(MIME_GEMTEXT));
    tui_status_clear();
    return 0;
}

struct fav_node *
favourites_find(const struct uri *u)
{
    char s[URI_STRING_MAX];
    uri_str(u, s, sizeof(s), URI_FLAGS_NO_TRAILING_SLASH_BIT);

    for (struct fav_node *n = s_head;
        n != NULL;
        n = n->link_n)
    {
        if (strncmp(s, n->uri, URI_STRING_MAX) == 0)
        {
            return n;
        }
    }

    return NULL;
}

void
favourites_push(struct fav_node *n)
{
    s_favs_modified = true;

    if (!s_head)
    {
        n->link_p = n->link_n = NULL;
        s_head = s_tail = n;
        return;
    }

    n->link_p = s_tail;
    n->link_n = NULL;
    s_tail->link_n = n;
    s_tail = n;
}

struct fav_node *
favourites_push_uri(
    const struct uri *restrict u,
    const char *restrict title,
    int title_len)
{
    struct fav_node *n = malloc(sizeof(struct fav_node));

    // We allocate maximum URI size for now to keep things simple
    n->uri = malloc(URI_STRING_MAX);
    uri_str(u, n->uri, URI_STRING_MAX, URI_FLAGS_NO_TRAILING_SLASH_BIT);

    if (title_len)
    {
        strncpy(n->title, title, min(title_len + 1, FAVOURITE_TITLE_MAX));
    }
    else
    {
        *n->title = '\0';
    }

    favourites_push(n);
    return n;
}

/* Delete favourite node from the list. */
void
favourites_delete(struct fav_node *n)
{
    if (n->link_p) n->link_p->link_n = n->link_n;
    else s_head = n->link_n;

    if (n->link_n) n->link_n->link_p = n->link_p;
    else s_tail = n->link_p;

    free(n->uri);
    free(n);

    s_favs_modified = true;
}

void
favourites_update_title(
    struct fav_node *restrict n,
    const char *restrict title,
    int title_len)
{
    strncpy(n->title,
        title,
        min(title_len + 1, FAVOURITE_TITLE_MAX));
    s_favs_modified = true;
}
