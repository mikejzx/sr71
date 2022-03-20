#ifndef UTIL_H
#define UTIL_H

#define min(x, y) \
    ((x) < (y) ? (x) : (y))
#define max(x, y) \
    ((x) > (y) ? (x) : (y))

#if 0
/*
 * UTF-8 decoding function
 * This is a bit stupid right now as it does a malloc.  Perhaps abstract it
 * away a little
 */
static inline
wchar_t *utf8_decode(const char *s)
{
    size_t len;
    wchar_t *ws;
    len = mbstowcs(NULL, s, 0);

    if (!(ws = malloc((len + 1) * sizeof(*ws))))
    {
        return NULL;
    }

    if (mbstowcs(ws, s, len) != (size_t)-1)
    {
        return ws;
    }
    free(ws);
    return NULL;
}

/*
 * Size of string without formatting chars
 * This needs to be seriously re-written to get rid of all these stupid goto's
 * https://gist.github.com/jart/b9104dd959f2dd65ecdcb5ce69d332e6
 */
static inline size_t
text_length(const char *str)
{
    int w;
    size_t sz = 0, i = 0;
    wchar_t *wcs = utf8_decode(str);
count:
    if (!wcs || wcs[i] == 0)
    {
        if (wcs) free(wcs);
        return sz;
    }

    if (wcs[i] == '\e') goto skip;

    ++i;
    w = wcwidth(wcs[i]);
    sz += w >= 0 ? w : 0;
    goto count;

skip:
    if (wcs[i] != 'm')
    {
        ++i;
        goto skip;
    }
    goto count;
}
#endif

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
    for (int x = 0; *s && x < n; ++x)
    {
        // Don't count escape sequences
        if (*s == '\e')
        {
            is_escape = true;
        }

        if (is_escape && *s != 'm')
        {
            // Skip counting until we reach the end of the format code
            continue;
        }
        is_escape = false;

        count += (*s++ & 0xC0) != 0x80;
    }
    return count;
}

#endif
