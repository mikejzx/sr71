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
        strncmp(uri + colon_pos, "://", 3) == 0)
    {
        char protocol_name[PROTOCOL_NAME_MAX];
        strncpy(protocol_name, uri, (size_t)colon_pos);
        result.protocol = lookup_protocol(protocol_name, colon_pos);
        protocol_name_len = (size_t)colon_pos + 3;
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
        // No path
        result.path[0] = '\0';
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
    struct uri *uri,
    char *buf,
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
        strcat(scheme, "://");
    }

    if (!uri->port ||
        flags & URI_FLAGS_NO_PORT_BIT)
    {
        // Print without port
        return snprintf(buf, buf_size,
            "%s%s%s%s",
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
uri_abs(struct uri *base, struct uri *rel)
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

    // Append path name
    // TODO: handle relative '../', etc. paths
    // TODO: avoid double '/'s in paths
    char old_path[URI_PATH_MAX];
    strncpy(old_path, rel->path, URI_PATH_MAX);
    snprintf(rel->path, URI_PATH_MAX,
        "%s/%s",
        base->path,
        old_path);
}

// Normalise/resolve a URI (i.e. get rid of '..', double slashes, etc.)
void
uri_normalise(struct uri *uri)
{

}
