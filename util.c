#include "pch.h"
#include "util.h"
#include "tui.h"

int
path_normalise(
    const char *restrict base,
    const char *restrict rel,
    char *restrict o)
{
    if (*base == '\0')
    {
        o[0] = '\0';
        return 0;
    }

    int o_size = 0;

    // Always start the result with a slash
    if (*base != '/')
    {
        o[o_size++] = '/';
    }

    bool parsed_rel = false;
    const char *c_last = base;
    size_t last_section_size = 0;
    for (const char *c = base;; ++c)
    {
        // Add each section one-by-one
        if ((*c == '/' || *c == '\0') &&
            (c - c_last) > 0)
        {
            // Redundant '/./' removal
            if (strncmp(c_last, ".", c - c_last) == 0)
            {
                // Detected a '.', just remove this section as it's reduntant
                for (c_last = c; *c_last == '/'; ++c_last);
                continue;
            }

            // '..' to navigate up a director
            if (strncmp(c_last, "..", c - c_last) == 0)
            {
                // Remove the last section/directory from the URI
                // (min'd with 1 to ensure that leading slash is preserved)
                o_size = max(o_size - last_section_size, 1);

                for (c_last = c; *c_last == '/'; ++c_last);
                continue;
            }

            // Copy path section
            last_section_size = c - c_last;
            strncpy(o + o_size, c_last, last_section_size);
            o_size += last_section_size;

            // Copy the path separater
            if (!(parsed_rel && *c == '\0'))
            {
                strcpy(o + o_size, "/");
                ++o_size;
                ++last_section_size;
            }

            // Skip over redundant slashes
            for (c_last = c; *c_last == '/'; ++c_last);
        }

        // End of string
        if (!*c)
        {
            if (parsed_rel)
            {
                // Finished parsing
                break;
            }
            parsed_rel = true;
            // Begin parsing relative section
            c = rel;
            for (c_last = c; *c_last == '/'; ++c_last);
            c = c_last;
        }
    }
    c_last = rel;

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
        is_escape = false;

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
    for (size_t count = 0; count < l && *s; ++bytes, ++s)
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
        is_escape = false;

        count += (*s & 0xC0) != 0x80;
    }
    return bytes;
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

    tui_status_prepare();
    tui_say("Looking up address ...");

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
            tui_status_prepare();
            tui_say("error: failed to create socket");
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
            tui_status_prepare();
            tui_say("error: failed to set socket timeout");
            sock = 0;
            continue;
        }

        tui_status_prepare();
        tui_say("Connecting ...");

        // Connect socket
        if (connect(sock, (struct sockaddr *)i->ai_addr,
            i->ai_addrlen) < 0)
        {
            tui_status_prepare();
            tui_printf("error: failed to connect to %s", hostname);
            sock = 0;
            continue;
        }
        connected = true;

        break;
    }
    if (!connected)
    {
        tui_status_prepare();
        if (res == NULL)
        {
            tui_printf("error: no addresses for '%s'", hostname);
        }
        else
        {
            tui_printf("error: could not connect to '%s'", hostname);
        }
        freeaddrinfo(res);
        return 0;
    }
    freeaddrinfo(res);
    return sock;
}
