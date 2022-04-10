#include "pch.h"
#include "hyphenate_alg.h"

static int s_hyphens[1024];
static int s_count;
static int s_cur;

/* Start hyphenating a word */
void
hyphenate(const char *word, size_t word_len)
{
    s_cur = 0;
    s_count = 0;

#if TYPESET_NO_HYPHENATION
    return;
#endif

    if (!word_len) return;

    // This is an extremely basic hyphenation algorithm that doesn't take into
    // account any pattern matching, etc., and hence produces awful results for
    // most words.  This is just temporary until a proper algorithm is
    // implemented
    for (const char *c = word; c < word + word_len; ++c)
    {
        // Don't hyphenate if there's an explicit hyphen in it and the word is
        // short enough
        if (*c == '-' && word_len < 24)
        {
            s_count = 0;
            s_cur = -1;
            return;
        }

        if (c - word > 3 &&
            c - word < word_len - 2 &&
            (c - word) % 2 == 0)
        {
            s_hyphens[s_count++] = c - word;
        }
    }
}

/* Get next hyphenation (-1 on end) */
int
hyphenate_get(void)
{
    if (s_cur == -1 || !s_count) return -1;
    if (s_cur + 1 > s_count)
    {
        s_cur = -1;
        return -1;
    }

    return s_hyphens[s_cur++];
}
