#ifndef UTIL_H
#define UTIL_H

#define min(x, y) \
    ((x) < (y) ? (x) : (y))
#define max(x, y) \
    ((x) > (y) ? (x) : (y))

/* Count number of code points in UTF-8 string */
/* https://stackoverflow.com/a/32936928 */
static inline size_t
utf8_strlen(const char *s)
{
    size_t count = 0;
    for (; *s; count += (*s++ & 0xC0) != 0x80);
    return count;
}

/* Same as above, but with fixed size */
static inline size_t
utf8_strnlen(const char *s, size_t n)
{
    size_t count = 0;
    for (int x = 0;
        *s && x < n;
        count += (*s++ & 0xC0) != 0x80, ++x);
    return count;
}

/* Also ignores formatting */
static inline size_t
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
static inline size_t
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

#endif
