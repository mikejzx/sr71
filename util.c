#include "pch.h"
#include "util.h"
#include "tui.h"

int
path_normalise(
    const char *restrict base,
    const char *restrict rel,
    char *restrict o)
{
    int o_size = 0;

    // Make sure the result begins with a slash
    o[o_size++] = '/';

    // Start the paths after their trailing slash if they have any any (we
    // insert them ourselves)
    for (; *base && *base == '/'; ++base);
    const char *rel_old = rel;
    for (; *rel && *rel == '/'; ++rel);

    const char *c_last;

    // Parse both the paths
    const char *paths[] = { base, rel, 0 };
    const char **path = paths;

    if (rel != rel_old)
    {
        // If the second URI begins with a slash, then we need to completely
        // skip the base URI (the name is misleading, as the "relative" URI is
        // technically now an "absolute" one, but whatever... let's just be
        // glad it works)
        ++path;
    }

    for (; *path != NULL; ++path)
    {
        c_last = *path;
        for (const char *c = *path;; ++c)
        {
            // We add each section one-by-one.
            bool is_section = (c - c_last > 0 && *c == '/') || *c == '\0';
            if (!is_section) continue;

            // Navigate up directory via '..'
            if (*c &&
                c - c_last >= strlen("..") &&
                strncmp(c_last, "..", c - c_last) == 0)
            {
                // Remove the last section/directory from URI.
                for (; o_size > 1 && o[o_size - 1] == '/'; --o_size);
                for (; o_size > 1 && o[o_size - 1] != '/'; --o_size);
                for (c_last = c; *c_last && *c_last == '/'; ++c_last);
                continue;
            }

            // Remove '/./'s as these are redundant
            if (*c &&
                c - c_last >= strlen(".") &&
                strncmp(c_last, ".", c - c_last) == 0)
            {
                // Skip past section
                for (c_last = c; *c_last && *c_last == '/'; ++c_last);
                continue;
            }

            // Copy the path section
            if (c - c_last > 0)
            {
                ssize_t section_size = max(c - c_last, 0);
                strncpy(o + o_size, c_last, section_size);
                o_size += section_size;
            }

            // Add a separator, except at end of the relative string
            if (!(*path == rel && *c == '\0') &&
                c - c_last > 0)
            {
                strcpy(o + o_size, "/");
                ++o_size;
                for (c_last = c; *c_last && *c_last == '/'; ++c_last);
            }

            // Set the last point, skipping over slashes
            for (c_last = c; *c_last && *c_last == '/'; ++c_last);

            if (*c == '\0') break;
        }
    }

    // Make sure the N-T is there
    o[o_size] = '\0';
    return o_size;
}

/* Connect a socket to a host address */
int
connect_socket_to(const char *hostname, int port)
{
    if (port == 0) return 0;
    int sock;

    // I'll never know why on Earth getaddrinfo takes a string here
    char port_str[5];
    snprintf(port_str, sizeof(port_str), "%04d", port);

    tui_status_say("Looking up address ...");

    // Get host addresses
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = IPPROTO_TCP;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    struct addrinfo *res = NULL;
    getaddrinfo(hostname, port_str, &hints, &res);

    // Connect to any of the addresses we can
    bool connected = false;
    for (struct addrinfo *i = res; i != NULL; i = i->ai_next)
    {
        // Create socket
        if ((sock = socket(
            i->ai_addr->sa_family,
            SOCK_STREAM, 0)) < 0)
        {
            tui_status_say("error: failed to create socket");

            sock = 0;
            continue;
        }

        // Setup timeout on socket
        static const struct timeval timeout =
        {
            .tv_sec = 5,
            .tv_usec = 0,
        };
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
            (char *)&timeout, sizeof(timeout)) < 0 ||
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
            (char *)&timeout, sizeof(timeout)) < 0)
        {
            tui_status_say("error: failed to set socket timeout");

            sock = 0;
            continue;
        }

        tui_status_say("Connecting ...");

        // Connect socket
        if (connect(sock, (struct sockaddr *)i->ai_addr,
            i->ai_addrlen) < 0)
        {
            tui_status_begin();
            tui_printf("error: failed to connect to %s", hostname);
            tui_status_end();

            sock = 0;
            continue;
        }
        connected = true;

        break;
    }
    if (!connected)
    {
        tui_status_begin();
        if (res == NULL)
        {
            tui_printf("error: no addresses for '%s'", hostname);
        }
        else
        {
            tui_printf("error: could not connect to '%s'", hostname);
        }
        tui_status_end();

        freeaddrinfo(res);
        return 0;
    }
    else
    {
        tui_printf("Connected.");
    }
    freeaddrinfo(res);
    return sock;
}

int
timestamp_age_human_readable(time_t ts, char *buf, size_t buf_len)
{
    time_t now = time(NULL);
    time_t diff = difftime(now, ts);

    if (diff < 60 * 2)
    {
        // In last minute or so
        static const char *STR_NOW = "now";
        strncpy(buf, STR_NOW, buf_len);
        return strlen(STR_NOW);
    }
    if (diff < 60 * 60)
    {
        // Within last hour; print in minutes
        return snprintf(buf, buf_len,
            "%d minutes ago",
            (int)diff / 60);
    }
    else if (diff < 60 * 60 * 2)
    {
        // Within last hour
        static const char *STR_1HOUR = "1 hour ago";
        strncpy(buf, STR_1HOUR, buf_len);
        return strlen(STR_1HOUR);
    }
    else if (diff < 60 * 60 * 24)
    {
        // Within last 24 hours; print in hours
        return snprintf(buf, buf_len,
            "%d hours ago",
            (int)diff / (60 * 60));
    }
    else if (diff < 60 * 60 * 24 * 2)
    {
        // Within last day
        static const char *STR_YESTERDAY = "yesterday";
        strncpy(buf, STR_YESTERDAY, buf_len);
        return strlen(STR_YESTERDAY);
    }
    else
    {
        // Print anything else in days
        return snprintf(buf, buf_len,
            "%d days ago",
            (int)diff / (60 * 60 * 24));
    }

    return 0;
}

int
timestamp_age_days_approx(time_t from, time_t to)
{
    time_t diff = difftime(to, from);

    return diff / (60 * 60 * 24);
}

/* Derived from
 * https://stackoverflow.com/questions/14834267/
 *         reading-a-text-file-backwards-in-c
 */
char *
getline_reverse(char *buf, int n, FILE *fp)
{
    long fpos;

    if (n <= 1 ||
        (fpos = ftell(fp)) == -1 ||
        !fpos)
    {
        return NULL;
    }

    int cpos = n - 1;
    buf[cpos] = '\0';

    for (int first = 1;;)
    {
        int c;
        if (fseek(fp, --fpos, SEEK_SET) != 0 ||
            (c = fgetc(fp)) == EOF)
        {
            return NULL;
        }

        // End on the new-line
        if (c == '\n' && first == 0)
        {
            break;
        }
        first = 0;

        if (c != '\r' && c != '\n')
        {
            unsigned char ch = c;
            if (cpos == 0)
            {
                memmove(buf + 1, buf, n - 2);
                ++cpos;
            }
            memcpy(buf + (--cpos), &ch, 1);
        }

        if (fpos == 0)
        {
            fseek(fp, 0, SEEK_SET);
            break;
        }
    }

    memmove(buf, buf + cpos, n - cpos);
    return buf;
}
