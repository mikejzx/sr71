#ifndef URI_H
#define URI_H

#include "gopher.h"

#define URI_HOSTNAME_MAX 256
#define URI_PATH_MAX 512
#define PROTOCOL_NAME_MAX 16
#define URI_STRING_MAX (URI_HOSTNAME_MAX + URI_PATH_MAX + PROTOCOL_NAME_MAX + 7)

enum uri_protocol
{
    PROTOCOL_NONE = 0,
    PROTOCOL_UNKNOWN,
    PROTOCOL_GEMINI,
    PROTOCOL_GOPHER,
    PROTOCOL_FINGER,
    PROTOCOL_FILE,

    // Special protocols which have no '//'
    PROTOCOL_MAILTO,
    //PROTOCOL_MAGNET,

    PROTOCOL_COUNT
};

static const char *PROTOCOL_NAMES[PROTOCOL_COUNT] =
{
    [PROTOCOL_NONE]    = "",
    [PROTOCOL_UNKNOWN] = "",
    [PROTOCOL_GEMINI]  = "gemini",
    [PROTOCOL_GOPHER]  = "gopher",
    [PROTOCOL_FINGER]  = "finger",
    [PROTOCOL_FILE]    = "file",
    [PROTOCOL_MAILTO]  = "mailto",
    //[PROTOCOL_MAGNET]  = "magnet",
};

/* Lookup a protocol from string */
static inline enum uri_protocol
lookup_protocol(const char *s, size_t l)
{
    for (int i = 0; i < PROTOCOL_COUNT; ++i)
    {
        if (strncmp(s, PROTOCOL_NAMES[i], l) == 0)
        {
            return (enum uri_protocol)i;
        }
    }
    return PROTOCOL_UNKNOWN;
}

/* Flags for when converting a URI to a string */
enum uri_string_flags
{
    // No flags; this is default and will print scheme, hostname, port (if
    // non-zero), and path
    URI_FLAGS_NONE = 0,

    // Exclude the port in the string.
    URI_FLAGS_NO_PORT_BIT = 1,

    // Include fancy URI styling/colouring escape codes Essentially this should
    // make the hostname appear in a brighter colour than the other parts of
    // the URI
    URI_FLAGS_FANCY_BIT = 2,

    // Do not include protocol in string
    URI_FLAGS_NO_PROTOCOL_BIT = 4,

    // Do not include any trailing slash in the string
    URI_FLAGS_NO_TRAILING_SLASH_BIT = 8,

    // Do not include Gopher item type on Gopher URIs
    URI_FLAGS_NO_GOPHER_ITEM_BIT = 16,
};

struct uri
{
    // The URI's protocol
    enum uri_protocol protocol;
    char protocol_str[PROTOCOL_NAME_MAX];

    // And the other stuff
    char hostname[URI_HOSTNAME_MAX];
    int port;
    char path[URI_PATH_MAX];

    // Gopher item type.  It's very convenient to store this here and keeps
    // stuff a bit cleaner
    enum gopher_item_type gopher_item;
};

struct uri uri_parse(const char *, size_t);

size_t uri_str(
    const struct uri *restrict const,
    char *restrict,
    size_t,
    enum uri_string_flags);

void uri_abs(struct uri *restrict, struct uri *restrict);

static inline int
uri_cmp(
    const struct uri *restrict const a,
    const struct uri *restrict const b)
{
    return (
        strncmp(a->hostname, b->hostname, URI_HOSTNAME_MAX) == 0 &&
        strncmp(a->path, b->path, URI_PATH_MAX) == 0 &&
        a->protocol == b->protocol) ? 0 : -1;
}

/* Compare URIs and ignore trailing slash */
static inline int
uri_cmp_notrailing(
    const struct uri *restrict const a,
    const struct uri *restrict const b)
{
    size_t len_a = strlen(a->path), len_b = strlen(b->path);
    if (a->path[len_a - 1] == '/') --len_a;
    if (b->path[len_b - 1] == '/') --len_b;

    return (
        len_a == len_b &&
        strncmp(a->hostname, b->hostname, URI_HOSTNAME_MAX) == 0 &&
        strncmp(a->path, b->path, len_a) == 0 &&
        a->protocol == b->protocol) ? 0 : -1;
}

#endif
