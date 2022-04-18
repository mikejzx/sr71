#include "pch.h"
#include "utf8.h"

static char *s_c;
static wchar_t *s_wc;
static size_t s_c_size, s_wc_size;

void
utf8_init(void)
{
    s_c_size = 256;
    s_wc_size = 512;
    s_c = malloc(s_c_size * sizeof(char));
    s_wc = malloc(s_wc_size * sizeof(wchar_t));
}

void
utf8_deinit(void)
{
    free(s_c);
    free(s_wc);
}

/* Calculate width of a string with multi-byte sequences and escapes */
size_t
utf8_width(const char *s_in, size_t n)
{
    if (!s_in || !n) return 0;

    int width = 0;

    // Check that we have enough space for a buffer
    size_t needed_c = n + 1;
    if (needed_c >= s_c_size)
    {
        free(s_c);
        s_c_size = (needed_c * 3) / 2;
        s_c = malloc(s_c_size * sizeof(char));
        if (!s_c) exit(-1);
    }

    // Copy the buffer, stripping escape codes and stripping certain characters
    // that result in incorrect width (i.e. tabs)
    bool is_escape = false;
    const char *end = s_in + n;
    char *s_cur = s_c;
    for (; s_in < end && *s_in; ++s_in)
    {
        // Detect escapes
        if (is_escape && *s_in != 'm') continue;
        if (*s_in == '\x1b')
        {
            is_escape = true;
            continue;
        }
        if (is_escape && *s_in == 'm')
        {
            is_escape = false;
            continue;
        }

        // Copy the character, though use space instead of tabs
        if (*s_in == '\t') *s_cur = ' ';
        else *s_cur = *s_in;
        ++s_cur;
    }
    *s_cur = '\0';

    // Check we have enough space for wide-string buffer
    size_t needed_wc = mbstowcs(NULL, s_c, 0) + 1;
    if (needed_wc >= s_wc_size)
    {
        free(s_wc);
        s_wc_size = (needed_wc * 3) / 2;
        s_wc = malloc(s_wc_size * sizeof(wchar_t));
        if (!s_wc) exit(-1);
    }

    // Perform wide string conversion
    if (mbstowcs(s_wc, s_c, needed_wc) == (size_t)-1) return 0;

    // Measure width
    width = wcswidth(s_wc, needed_wc);

    return width;
}

/* Number of bytes that exist in a length (inverse of above) */
size_t
utf8_size_w_formats(const char *s, size_t l)
{
    if (!s || !l) return 0;

    bool is_escape = false;
    int bytes = 0;
    for (size_t count = 0; *s && count <= l; ++bytes, ++s)
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
    return bytes;
}

const char *
next_char_w_formats(
    const char *restrict c,
    const char *restrict end)
{
    // Move over one char initially
    ++c;

    // Skip over escape codes
    bool is_escape = false;
    for (is_escape = *c == '\x1b'; is_escape && c < end; ++c)
    {
        if (*c == 'm')
        {
            // Last char of escape
            return c + 1;
        }
    }
    return c;
}
