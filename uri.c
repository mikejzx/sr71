#include "pch.h"
#include "uri.h"

struct uri
uri_parse(const char *uri, size_t uri_len)
{
    struct uri result;
    memset(&result, 0, sizeof(struct uri));
    if (!uri_len) return result;

    // Find protocol:
    int colon_pos = strcspn(uri, ":");
    size_t protocol_name_len = 0;
    if (colon_pos < uri_len &&
        (strncmp(uri + colon_pos, "://", strlen("://")) == 0 ||
         strncmp(uri, PROTOCOL_NAMES[PROTOCOL_MAILTO], colon_pos) == 0))
    {
        strncpy(result.protocol_str, uri, (size_t)colon_pos);
        result.protocol = lookup_protocol(result.protocol_str, colon_pos);
        switch(result.protocol)
        {
        case PROTOCOL_MAILTO:
            protocol_name_len = (size_t)colon_pos + strlen(":");
            break;
        default:
            protocol_name_len = (size_t)colon_pos + strlen("://");
            break;
        }
    }

    // Get optional port (0 if none provided)
    colon_pos = strcspn(uri + protocol_name_len, ":") + protocol_name_len;
    if (colon_pos < (uri_len - 1))
    {
        result.port = atoi(uri + colon_pos + 1);
    }
    else
    {
        colon_pos = -1;
    }

    // Find where path would start
    // Will be end of string if there's no path
    int path_pos = 0;
    if (protocol_name_len > 0)
    {
        for (path_pos = protocol_name_len;
            path_pos < uri_len && uri[path_pos] != '/';
            ++path_pos);
    }

    // Use path position instead of colon (if no port was given)
    if (colon_pos < 0) colon_pos = path_pos;

    // Get hostname for non-relative URIs
    if (path_pos > 0 && protocol_name_len > 0)
    {
        // There's a port in the string, so the hostname is between the colon
        // and the protocol name sections
        size_t start_pos = protocol_name_len;
        strncpy(result.hostname,
            uri + start_pos, (size_t)colon_pos - start_pos);
    }

    // Get path
    if (path_pos < uri_len)
    {
        strncpy(result.path, uri + path_pos, uri_len - path_pos);
    }
    else
    {
        // Empty path
        result.path[0] = '/';
        result.path[1] = '\0';
    }

#if 0
    printf("Parsed URI: '%s'\n"
        "  Protocol: %s\n"
        "  Host    : %s\n"
        "  Port    : %d\n"
        "  Path    : %s\n",
        uri, PROTOCOL_NAMES[result.protocol],
        result.hostname, result.port, result.path);
#endif
    return result;
}

/* Convert URI to string (returns size of result) */
size_t
uri_str(
    struct uri *restrict uri,
    char *restrict buf,
    size_t buf_size,
    enum uri_string_flags flags)
{
    if (!uri) return 0;

    // Get scheme string
    char scheme[PROTOCOL_NAME_MAX + strlen("://")];
    scheme[0] = '\0';
    if (!(uri->protocol == PROTOCOL_NONE ||
        uri->protocol == PROTOCOL_UNKNOWN ||
        flags & URI_FLAGS_NO_PROTOCOL_BIT))
    {
        strcpy(scheme, PROTOCOL_NAMES[uri->protocol]);
        switch(uri->protocol)
        {
        case PROTOCOL_MAILTO:
            strcat(scheme, ":");
            break;
        default:
            strcat(scheme, "://");
            break;
        }
    }
    else if (uri->protocol == PROTOCOL_UNKNOWN && *uri->protocol_str)
    {
        strcpy(scheme, uri->protocol_str);
        strcat(scheme, "://");
    }

    if (!uri->port ||
        flags & URI_FLAGS_NO_PORT_BIT)
    {
        const char *fmt = "%s" // Scheme
            "%s"               // Hostname
            "%s%s";            // Path
        if (flags & URI_FLAGS_FANCY_BIT)
        {
            // Print some escapes with it
            fmt = "\x1b[2m%s\x1b[0m"
                "%s"
                "\x1b[2m%s%s\x1b[0m";
        }

        // Print without port
        return snprintf(buf, buf_size, fmt,
            scheme,
            uri->hostname,
            (*uri->path != '/' && *uri->path != '\0') ? "/" : "",
            uri->path);
    }
    else
    {
        // Print with port
        return snprintf(buf, buf_size,
            "%s%s:%d%s%s",
            scheme,
            uri->hostname,
            uri->port,
            (*uri->path != '/' && *uri->path != '\0') ? "/" : "",
            uri->path);
    }
}

/* Make a relative URI absolute */
void
uri_abs(struct uri *restrict base, struct uri *restrict rel)
{
    if (rel->protocol != PROTOCOL_NONE ||
        rel->hostname[0] != '\0')
    {
        // Not relative
        // TODO: make sure this is actually what defines a relative URI
        return;
    }

    rel->protocol = base->protocol;
    rel->port     = base->port;
    strncpy(rel->hostname, base->hostname, URI_HOSTNAME_MAX);

    // Append and normalise paths
    char old_path[URI_PATH_MAX];
    strncpy(old_path, rel->path, URI_PATH_MAX);
    path_normalise(base->path, old_path, rel->path);
}
