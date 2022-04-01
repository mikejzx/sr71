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

    // Gopher-specific: it is common these days to prefix paths with the
    //                  resource item type,
    //                    e.g. gopher://foo.bar/0/a_text_file.txt
    //                  so we have to accomodate for this for some links to
    //                  work correctly
    int path_len = max(uri_len - path_pos, 0);
    if (result.protocol == PROTOCOL_GOPHER)
    {
        enum gopher_item_type item = GOPHER_ITEM_UNSUPPORTED;
        if (path_len >= 3 &&
            uri[path_pos] == '/' &&
            (item = gopher_item_lookup(uri[path_pos + 1]))
                != GOPHER_ITEM_UNSUPPORTED &&
            uri[path_pos + 2] == '/')
        {
            path_pos += 2;
            path_len -= 2;
        }
        result.gopher_item = item;
    }

    // Get path
    if (path_pos < uri_len)
    {
        strncpy(result.path, uri + path_pos, path_len);
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
    const struct uri *restrict const uri,
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

    int path_len = strlen(uri->path);
    if (flags & URI_FLAGS_NO_TRAILING_SLASH_BIT &&
        uri->path[path_len - 1] == '/')
    {
        path_len = max(path_len - 1, 0);
    }

    // Apply gopher item prefix
    char gopher_item_prefix[12];
    if (uri->protocol == PROTOCOL_GOPHER &&
        !(flags & URI_FLAGS_NO_GOPHER_ITEM_BIT) &&
        uri->gopher_item != GOPHER_ITEM_UNSUPPORTED)
    {
        if (flags & URI_FLAGS_FANCY_BIT)
        {
            gopher_item_prefix[0]  = '/';
            gopher_item_prefix[1]  = '\x1b';
            gopher_item_prefix[2]  = '[';
            gopher_item_prefix[3]  = '3';
            gopher_item_prefix[4]  = '3';
            gopher_item_prefix[5]  = 'm';
            gopher_item_prefix[6]  = GOPHER_ITEM_IDS[uri->gopher_item];
            gopher_item_prefix[7]  = '\x1b';
            gopher_item_prefix[8]  = '[';
            gopher_item_prefix[9]  = '0';
            gopher_item_prefix[10] = 'm';
            gopher_item_prefix[11] = '\0';
        }
        else
        {
            gopher_item_prefix[0]  = '/';
            gopher_item_prefix[1]  = GOPHER_ITEM_IDS[uri->gopher_item];
            gopher_item_prefix[2]  = '\0';
        }
    }
    else
    {
        gopher_item_prefix[0] = '\0';
    }

    const char *fmt;
    if (!uri->port ||
        flags & URI_FLAGS_NO_PORT_BIT)
    {
        fmt = "%s"   // Scheme
            "%s"     // Hostname
            "%s"     // Gopher item prefix
            "%.*s";  // Path
        if (flags & URI_FLAGS_FANCY_BIT)
        {
            // Fancy mode escapes
            fmt = "\x1b[2m%s\x1b[0m"
                "%s"
                "%s"
                "\x1b[2m%.*s\x1b[0m";
        }

        // Print without port
        return snprintf(buf, buf_size, fmt,
            scheme,
            uri->hostname,
            gopher_item_prefix,
            path_len,
            uri->path);
    }
    else
    {
        fmt = "%s"   // Scheme
            "%s"     // Hostname
            ":%d"    // Port
            "%s"     // Gopher item prefix
            "%.*s";  // Path
        if (flags & URI_FLAGS_FANCY_BIT)
        {
            // Fancy mode escapes
            fmt = "\x1b[2m%s\x1b[0m"
                "%s"
                ":%d"
                "%s"
                "\x1b[2m%.*s\x1b[0m";
        }

        // Print with port
        return snprintf(buf, buf_size,
            fmt,
            scheme,
            uri->hostname,
            uri->port,
            gopher_item_prefix,
            path_len,
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
