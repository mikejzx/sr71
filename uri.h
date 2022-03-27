#ifndef URI_H
#define URI_H

#define URI_HOSTNAME_MAX 256
#define URI_PATH_MAX 512
#define PROTOCOL_NAME_MAX 16

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

    // Include fancy URI styling/colouring escape codes (TODO implement this).
    // Essentially this should make the hostname appear in a brighter colour
    // than the other parts of the URI
    URI_FLAGS_FANCY_BIT = 2,

    // Do not include protocol in string
    URI_FLAGS_NO_PROTOCOL_BIT = 4,
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
};

struct uri uri_parse(const char *, size_t);
size_t uri_str(struct uri *restrict, char *restrict,
    size_t, enum uri_string_flags);
void uri_abs(struct uri *restrict, struct uri *restrict);

#endif
