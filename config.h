#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h
 *
 * Configuration options for 'frosty'.  At the moment everything is configured
 * at compile-time (suckless style)
 */

/*
 * Pager options
 */

// Preferred width of the page content (applies to Gemtext)
static const int CONTENT_WIDTH_PREFERRED = 70;

// Bias of content in the page.
//   0.0: fully left
//   0.5: middle
//   1.0: fully right
static const float CONTENT_MARGIN_BIAS = 0.5f;

/*
 * Formatting, escapes, colours, etc.
 */
#define COLOUR_STATUS_LINE "\x1b[2m"
#define COLOUR_PAGER_LINK_SELECTED "\x1b[35m"
#define COLOUR_PAGER_LINK "\x1b[34m"
#define COLOUR_HEADING1 "\x1b[1;36m"
#define COLOUR_HEADING2 "\x1b[36m"
#define COLOUR_HEADING3 "\x1b[1m"

// Whether to do fancy highlighting in URI of status line.  This will e.g. make
// hostname in a brighter colour, and will highlight gopher item type.
#define STATUS_LINE_FANCY_URI_FORMAT 1

#define LIST_BULLET_CHAR "\u2022"
#define BLOCKQUOTE_PREFIX "> "

/* Offsets from left margin */
#define GEMTEXT_INDENT_PARAGRAPH 1
#define GEMTEXT_INDENT_HEADING 0
#define GEMTEXT_INDENT_VERBATIM 2
#define GEMTEXT_INDENT_BLOCKQUOTE 2
#define GEMTEXT_INDENT_LIST 2

/* Fancy indent looks like this:
 *   This is a paragraph which has been indented.  By default we only indent
 * paragraphs which are not directly after a heading.  Set to 'always' to always
 * indent the paragraphs, whether they are under headings or not.
 */
#define GEMTEXT_FANCY_PARAGRAPH_INDENT 2
#define GEMTEXT_FANCY_PARAGRAPH_INDENT_ALWAYS 0

// Set to 1 to use vi-style tilde at end of buffer
#define CLEAR_VI_STYLE 1
#define VI_EMPTY_CHAR_STR "\x1b[2m~\x1b[0m"

/*
 * Cache
 */

// Build with persistent disk caching
#define CACHE_USE_DISK 1

/*
 * Paths
 */

// For now all data is just stored in current directory
//#define PROGRAM_DATA_DIR "~/.local/share/" PROGRAM_NAME "/"
#define PROGRAM_DATA_DIR "."

/*
 * Typesetter options
 */

/* Set to 1 to use a "greedy" line-breaking algorithm, instead of Knuth-Plass */
#define TYPESET_LINEBREAK_GREEDY 0

/* Whether to justify text to the margins */
#define TYPESET_JUSTIFY 0

/* Set to 1 to disable hyphenation algorithm */
#define TYPESET_NO_HYPHENATION 0

/*
 * Force double spaces after sentences.  For now we disable this for
 * non-justified text in hopes that it will reduce the number of spaces; though
 * double spaces should still appear when justified as the justification
 * algorithm prioritises placing spaces after sentences.
 */
#if TYPESET_JUSTIFY
#  define TYPESET_FORCE_DOUBLE_SPACE_SENTENCE 0
#else
#  define TYPESET_FORCE_DOUBLE_SPACE_SENTENCE 1
#endif

/* Penalties/bonuses for line-breaking algorithm */
#define TYPESET_LB_PENALTY_HYPHENATION 20
#define TYPESET_LB_PENALTY_HYPHENATION_EXPLICIT 0
#define TYPESET_LB_PENALTY_END_OF_SENTENCE_BONUS -15

/*
 * Misc.
 */

/* Set to 0 to disable quit confirmation message */
#define TUI_QUIT_CONFIRMATION 1

#endif
