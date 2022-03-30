#ifndef MIME_H
#define MIME_H

#define MIME_TYPE_MAX 128

#define MIME_GEMTEXT "text/gemini"
#define MIME_GOPHERMAP "text/gophermap"
#define MIME_PLAINTEXT "text/plain"

struct mime
{
    // Full MIME type string
    char str[MIME_TYPE_MAX];

    // Location of '/' in string
    int sep;
};

/* Parse a MIME type */
static inline void
mime_parse(struct mime *m, const char *s, size_t s_len)
{
    // Copy full string
    const size_t len = min(MIME_TYPE_MAX, s_len + 1);
    strncpy(m->str, s, len);
    m->str[len - 1] = '\0';

    // Get location of separator
    for (const char *x = m->str;; ++x)
    {
        if (!*x || x >= s + s_len)
        {
            m->sep = -1;
            break;
        }

        if (*x == '/')
        {
            m->sep = x - m->str;
            break;
        }
    }
}

/* Check if MIME is equal to a string */
static inline bool
mime_eqs(
    const struct mime *restrict const m,
    const char *restrict s)
{
    return strcmp(m->str, s) == 0;
}

#endif
