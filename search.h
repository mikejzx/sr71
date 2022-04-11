#ifndef SEARCH_H
#define SEARCH_H

#include "pager.h"

/*
 * search.h
 *
 * All the code related to searching functionality
 */

struct search_match
{
    // We store the range where the match begins/ends
    struct
    {
        // Index in the *typeset* buffer where the match was found.  We
        // use the typeset buffer rather than the raw one because the
        // raw one creates way too many issues to try and deal with
        // (i.e. unformatted text is present, and it's a royal pain in
        // the ass to work out where the match is in the typeset buffer
        // to highlight it)
        unsigned line;

        // Location of the beginning/end in the line's buffer pointer
        const char *loc;
    } begin, end;
};

struct search
{
    // The last search query performed
    char query[256];
    unsigned query_len;

    // Current list of search matches.
    struct search_match *matches;
    unsigned match_count;
    unsigned match_capacity;

    // Whether the match locations need to be updated due to the typeset
    // buffer changing (e.g. on window width resize)
    bool invalidated;

    // Index of most recent match (e.g. via 'n'/'N' keys)
    int index;

    // Whether we are searching in reverse (via '?')
    bool reverse;
};

void search_init(void);
void search_deinit(void);
void search_reset(void);
void search_update(void);
void search_perform(void);
void search_next(void);
void search_prev(void);
void search_highlight_matches(void);

#endif
