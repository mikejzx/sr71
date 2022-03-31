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

/* Count number of code points in UTF-8 string */
/* https://stackoverflow.com/a/32936928 */
size_t
utf8_strlen(const char *s)
{
    size_t count = 0;
    for (; *s; count += (*s++ & 0xC0) != 0x80);
    return count;
}

/* Same as above, but with fixed size */
size_t
utf8_strnlen(const char *s, size_t n)
{
    size_t count = 0;
    for (int x = 0;
        *s && x < n;
        count += (*s++ & 0xC0) != 0x80, ++x);
    return count;
}

/* Also ignores formatting */
size_t
utf8_strnlen_w_formats(const char *s, size_t n)
{
    size_t count = 0;
    bool is_escape = false;
    for (int x = 0; x < n && *s; ++x, ++s)
    {
        if (is_escape && *s != 'm')
        {
            // Skip counting until we reach the end of the format code
            continue;
        }

        // Don't count escape sequences
        if (*s == '\x1b')
        {
            is_escape = true;
            continue;
        }
        if (is_escape && *s == 'm')
        {
            is_escape = false;
            continue;
        }

        count += (*s & 0xC0) != 0x80;
    }
    return count;
}

/* Number of bytes that exist in a length (inverse of above) */
size_t
utf8_size_w_formats(const char *s, size_t l)
{
    bool is_escape = false;
    int bytes = 0;
    for (size_t count = 0; count <= l && *s; ++bytes, ++s)
    {
        if (is_escape && *s != 'm')
        {
            continue;
        }

        if (*s == '\x1b')
        {
            is_escape = true;
            continue;
        }
        if (is_escape && *s == 'm')
        {
            is_escape = false;
            continue;
        }

        count += (*s & 0xC0) != 0x80;
    }
    return max(bytes - 1, 0);
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
    freeaddrinfo(res);
    return sock;
}
