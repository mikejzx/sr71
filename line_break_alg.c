#include "pch.h"
#include "line_break_alg.h"
#include "hyphenate_alg.h"

#define LINEBREAK_INITIAL_ITEM_COUNT 4096
#define LINEBREAK_INITIAL_BREAKPOINT_COUNT 64

#define LB_INFINITY (SHRT_MAX + 1)

// Represents a single "item" in a string of text.  All algorithms are based
// around the fundamental 'box-glue-penalty' model as used in Knuth-Plass
// We try to pack this to 16 bytes to slightly reduce some memory usage
struct lb_item
{
    union
    {
        // Fixed-width item that can be typeset within the paragraph.
        // Usually we use boxes to store word fragments
        struct __attribute__((__packed__)) lb_box
        {
            // Content of the box; the number of chars will be the width of the
            // box
            const char *content;

            // The true width (in bytes) of the content to be displayed.  E.g.
            // UTF-8 text may have more bytes here, while having a shorter 'w'
            // width
            uint8_t w_canon;

            // Aligns the whole outer structure to 16 bytes (and this specific
            // structure to 14 bytes)
            uint8_t _padding[5];
        } b;

        struct lb_glue
        {
            bool no_stretch;
        } g;

        // Glue is used to "stick the boxes together" and are what essentially
        // define the space.  We have no reason to use stretch/shrink
        // properties as defined in Knuth-Plass, as our implementation does not
        // need to support proportional fonts.

        // Penalties can be used to define breakpoints within the paragraph;
        // (e.g. hyphenation points) though have a number associated with them
        // to indicate the desirability of that break
        struct lb_penalty
        {
            int16_t penalty;
            int8_t flag;
        } p;
    };

    // Type of item
    enum __attribute__((__packed__)) lb_item_type
    {
        LB_BOX = 0,
        LB_GLUE,
        LB_PENALTY,
    } t;

    // All items types store a width
    uint8_t w;
};

// Knuth-Plass linked list node
struct kp_node
{
    // Position of break
    int pos;

    // Calculated score/demerits of break
    int score;

    // Number of the line that ends on this breakpoint
    int line;

    // Paragraph width sum
    int w;

    // Pointer to the *best previous node* in the list
    struct kp_node *prev;

    // Pointer to the next/prev node in the linked list
    struct kp_node *link_n, *link_p;
};

// Knuth-Plass linked list, for the "active" breakpoints
struct kp_ll
{
    struct kp_node *head, *tail;
};

// Breakpoint definition
struct lb_breakpoint
{
    // Position that break occurred
    uint16_t pos;
};

// Current item list
static struct lb_item *s_items;
static size_t s_icap;
static int s_icount;

// Breakpoint list
static struct lb_breakpoint *s_bp;
static size_t s_bpcap;
static int s_bpcount;
static int s_bp_cur;
static bool s_bp_reversed = false;

// Knuth-Plass
static struct kp_ll s_kp_active, s_kp_inactive;
static int s_kp_width_sum;
static void knuth_plass(const struct lb_item *, int);

// Misc
static int s_linelen_initial, s_linelen_follow;
static bool word_is_end_of_sentence(const char *, size_t);
static void justify_text(struct lb_item *restrict, struct lb_item *restrict);

static inline void
ensure_item_buffer(size_t len)
{
    if (len < s_icap) return;

    s_icap = (len * 3) / 2;
    void *tmp = realloc(s_items, s_icap * sizeof(struct lb_item));
    if (!tmp)
    {
        fprintf(stderr, "out of memory\n");
        free(s_items);
        exit(-1);
    }
    s_items = tmp;
}

static inline void
ensure_item_buffer_incr(void) { ensure_item_buffer(s_icount + 3); }

static inline void
ensure_breakpoint_buffer(size_t len)
{
    if (len < s_bpcap) return;

    s_bpcap = (len * 3) / 2;
    void *tmp = realloc(s_bp, s_bpcap * sizeof(struct lb_breakpoint));
    if (!tmp)
    {
        fprintf(stderr, "out of memory\n");
        free(s_bp);
        exit(-1);
    }
    s_bp = tmp;
}

static inline void
ensure_breakpoint_buffer_incr(void) { ensure_breakpoint_buffer(s_bpcount + 3); }

void
line_break_init(void)
{
    s_icount = 0;
    s_icap = LINEBREAK_INITIAL_ITEM_COUNT;
    s_items = malloc(s_icap * sizeof(struct lb_item));

    s_bpcap = LINEBREAK_INITIAL_BREAKPOINT_COUNT;
    s_bpcount = 0;
    s_bp = malloc(s_bpcap * sizeof(struct lb_breakpoint));

    memset(&s_kp_active, 0, sizeof(struct kp_ll));
    memset(&s_kp_inactive, 0, sizeof(struct kp_ll));
}

void
line_break_deinit(void)
{
    free(s_items);
    free(s_bp);
}

/* Prepare buffers, etc. for breaking a new paragraph */
void
line_break_prepare(const struct lb_prepare_args *args)
{
    // Reset item count
    s_icount = 0;

    s_linelen_initial = args->length - args->skip;
    s_linelen_follow = args->length - args->hang;

    // Resize buffer to an approximate initial size
    ensure_item_buffer(args->line_end - args->line);

    const char
        *c_last = args->line + args->offset,
        *c = c_last,
        *c_word;

    // Move over leading whitespace
    for (c_last = c; *c_last == ' ' || *c_last == '\t'; ++c_last);

    // Add indent box
    ensure_item_buffer_incr();
    s_items[s_icount++] = (struct lb_item)
    {
        .t = LB_BOX,
        .w = args->indent
    };

    // Begin converting the paragraph into the primitive structure
    for (;; ++c)
    {
        if (c - c_last <= 0) continue;

        // Spaces delimit words
        if (strchr(" \t\r\n-", *c) == NULL &&
            *c != '\0' &&
            c != args->line_end) continue;

        // Count explicit hyphens
        int hyphen_count = 0;
        for (; c[hyphen_count] == '-'; ++hyphen_count);

        // End of word (minus punctuation)
        for (c_word = c - 1;
            c_word >= c_last && (isspace(*c_word) || ispunct(*c_word));
            --c_word);
        ++c_word;

        // Find the hyphenation points in the word, and add them as penalty
        // items between the boxes
        hyphenate(c_last, c_word - c_last);
        int h_last = 0;
        for (int h = hyphenate_get(); h != -1; h = hyphenate_get())
        {
            ensure_item_buffer_incr();
            s_items[s_icount++] = (struct lb_item)
            {
                .t = LB_BOX,
                .w = utf8_width(c_last + h_last, h - h_last),
                .b =
                {
                    .content = c_last + h_last,
                    .w_canon = h - h_last,
                }
            };
            ensure_item_buffer_incr();
            s_items[s_icount++] = (struct lb_item)
            {
                .t = LB_PENALTY,
                .w = 1,
                .p = { .penalty = TYPESET_LB_PENALTY_HYPHENATION, .flag = true }
            };
            h_last = h;
        }
        // Final box for word
        ensure_item_buffer_incr();
        s_items[s_icount++] = (struct lb_item)
        {
            .t = LB_BOX,
            .w = utf8_width(c_last + h_last, (c_word - c_last) - h_last),
            .b =
            {
                .content = c_last + h_last,
                .w_canon = (c_word - c_last) - h_last,
            }
        };

        // Add punctuation in it's own box
        if (c_word < c)
        {
            ensure_item_buffer_incr();
            s_items[s_icount++] = (struct lb_item)
            {
                .t = LB_BOX,
                //.w = c - c_word,
                .w = 0,
                .b =
                {
                    .content = c_word,
                    .w_canon = c - c_word,
                }
            };
        }

        // Explicit hyphens/dashes get followed by a flagged penalty item with
        // width zero
        if (hyphen_count)
        {
            for (int h = 0; h < hyphen_count; ++h)
            {
                ensure_item_buffer_incr();
                s_items[s_icount++] = (struct lb_item)
                {
                    .t = LB_BOX,
                    // Width is zero because we move it into a following glue
                    // so it can hang in margins
                    .w = 0,
                    .b =
                    {
                        .content = "-",
                        .w_canon = 1,
                    },
                };

                // And an extra glue so that the hyphen can hang properly
                ensure_item_buffer_incr();
                s_items[s_icount++] = (struct lb_item)
                {
                    .t = LB_GLUE,
                    .w = 1,
                    // We don't want this glue to be stretched at all, or else
                    // there may be gaps after hyphens in-sentence, like- this
                    .g.no_stretch = true,
                };
            }
            ensure_item_buffer_incr();
            s_items[s_icount++] = (struct lb_item)
            {
                .t = LB_PENALTY,
                .w = 0,
                .p = { .penalty = 0, .flag = true },
            };
        }

        // Move over trailing whitespace
        for (c_last = c;
            *c_last && (*c_last == ' ' ||
                        *c_last == '\t' ||
                        *c_last == '-');
            ++c_last);

        // Add the glue for space
        if (c_last > c && !hyphen_count)
        {
            // Add two spaces for end of sentence.
            const struct lb_item *last_box = NULL;
            for (int x = s_icount - 1; x > 0; --x)
            {
                if (s_items[x].t == LB_BOX &&
                    s_items[x].b.content)
                {
                    last_box = &s_items[x];
                    break;
                }
            }
            bool end_of_sentence = word_is_end_of_sentence(
                last_box->b.content, last_box->b.w_canon);

            ensure_item_buffer_incr();
            s_items[s_icount++] = (struct lb_item)
            {
                .t = LB_GLUE,
                .w =
            #if TYPESET_FORCE_DOUBLE_SPACE_SENTENCE
                    end_of_sentence ? 2 :
            #endif
                    1 +
                    // Add additional space into glue for hanging punctuation
                    (c - c_word),
            };

            // Add penalty "bonus" after sentences to encourage sentences to
            // begin at start of line instead of end
            if (end_of_sentence)
            {
                ensure_item_buffer_incr();
                s_items[s_icount++] = (struct lb_item)
                {
                    .t = LB_PENALTY,
                    .p =
                    {
                        .penalty = TYPESET_LB_PENALTY_END_OF_SENTENCE_BONUS
                    }
                };
            }
        }

        if (!*c ||
            *c == '\r' ||
            *c == '\n' ||
            c == args->line_end) break;
    }

    // Paragraph ends with 'finishing glue' and a penalty item for the required
    // end-of-paragrah break
    ensure_item_buffer_incr();
    s_items[s_icount++] = (struct lb_item)
    {
        .t = LB_GLUE,
        .w = 0,
    };
    ensure_item_buffer_incr();
    s_items[s_icount++] = (struct lb_item)
    {
        .t = LB_PENALTY,
        .w = 0,
        .p =
        {
            .penalty = -LB_INFINITY
        }
    };
}

/*
 * Write the next broken line to the given buffer
 * Returns point to which buffer was written
 */
ssize_t
line_break_get(char *buf, size_t buf_len)
{
    struct lb_item *first, *last, *last_box;

    if (!s_bp_reversed)
    {
        if (s_bp_cur == 0) first = &s_items[0];
        else first = &s_items[s_bp[s_bp_cur - 1].pos];

        last = &s_items[s_bp[s_bp_cur].pos];
    }
    else
    {
        first = &s_items[s_bp[s_bpcount - s_bp_cur].pos];
        last = &s_items[s_bp[s_bpcount - (s_bp_cur + 1)].pos];
    }

    // Start and end on boxes only
    for (last_box = last;
        last_box > first && last_box->t != LB_BOX;
        --last_box);
    for (; first < last_box && first->t != LB_BOX; ++first);

    // Justify the content
    justify_text(first, last_box);

    /*
     * And finally, draw the items out in reverse; we do this so that items
     * towards left are drawn "on top", e.g. if their width is smaller than
     * their canonical width.  This is done so that hanging punctuation, which
     * has zero-width, is drawn properly without following glue overwriting it
     */

    char *buf_abs_end = buf + buf_len;
    char *buf_end;
    char *buf_ptr = buf;
    const struct lb_item *item;

    // First iterate to find the total width to start the buffer point at and
    // move backwards from
    for (item = first; item <= last; ++item)
    {
        switch(item->t)
        {
        case LB_BOX:
            if (item->b.content == NULL)
            {
                buf_ptr += item->w;
                break;
            }
            if (item->w) buf_ptr += item->b.w_canon;

            break;
        case LB_GLUE:
            if (item == first || item == last) continue;
            buf_ptr += item->w;
            break;
        case LB_PENALTY:
            if (item != last || !item->w) continue;
            buf_ptr += 1;
            break;
        default: break;
        }
    }

    // Give up if we exceed the end of the given buffer
    if (buf_ptr >= buf_abs_end)
    {
        ++s_bp_cur;
        return 0;
    }

    buf_end = buf_ptr;

    // Draw the items
    for (item = last; item >= first; --item)
    {
        switch(item->t)
        {
        case LB_BOX:
            if (item->b.content == NULL)
            {
                // Write spaces for empty boxes (e.g. indent)
                for (int x = 0; x < item->w; ++x)
                {
                    --buf_ptr;
                    *buf_ptr = ' ';
                }
                break;
            }
            if (item->w) buf_ptr -= item->b.w_canon;

            // Relocate buffer end point if we write past it (e.g. hanging
            // punctuation)
            if (buf_ptr + item->b.w_canon > buf_end)
            {
                buf_end = buf_ptr + item->b.w_canon;
            }

            strncpy(buf_ptr, item->b.content, item->b.w_canon);
            break;
        case LB_GLUE:
            if (item == first || item == last) continue;
            for (int x = 0; x < item->w; ++x)
            {
                --buf_ptr;
                *buf_ptr = ' ';
            }
            break;
        case LB_PENALTY:
            if (item != last || !item->w) continue;
            --buf_ptr;
            *buf_ptr = '-';
            break;
        default: break;
        }
    }

    ++s_bp_cur;
    return buf_end - buf_ptr;
}

/* Return true if there's more lines to read */
bool
line_break_has_data(void)
{
    if (s_bp_cur + 1 > s_bpcount) return false;
    return true;
}

/*
 * "Greedy" line-breaking algorithm.  This is the simplest method and simply
 * breaks at latest space (or penalty) whenever a word will exceed the line
 * width
 */
void
line_break_compute_greedy(void)
{
    s_bpcount = 0;
    s_bp_reversed = false;

    int linelen = s_linelen_initial;
    int w = 0;
    const struct lb_item *last_box = NULL;
    for (int i = 0; i < s_icount; ++i)
    {
        const struct lb_item *item = &s_items[i];

        // Check for forced break
        if (item->t == LB_PENALTY &&
            item->p.penalty == -LB_INFINITY)
        {
            s_bp[s_bpcount].pos = (last_box ? (last_box - s_items) : i) + 1;
            ensure_breakpoint_buffer_incr();
            ++s_bpcount;
            w = 0;
            last_box = NULL;
            linelen = s_linelen_follow;
            continue;
        }

        if (item->t != LB_BOX) continue;

        if (w + item->w >= linelen)
        {
            s_bp[s_bpcount].pos = (last_box ? (last_box - s_items) : i) + 1;

            ensure_breakpoint_buffer_incr();
            ++s_bpcount;
            w = 0;
            last_box = NULL;
            linelen = s_linelen_follow;
        }

        // Add width of box
        w += item->w;

        // Add width of glue
        if (last_box)
        {
            for (++last_box; last_box < item; ++last_box)
            {
                if (last_box->t == LB_GLUE) w += last_box->w;
            }
        }

        last_box = item;
    }

    s_bp_cur = 0;
}

/*
 * Knuth-Plass line breaking algorithm, as described in Breaking Paragraphs in-
 * to Lines (1981).
 */
void
line_break_compute_knuth_plass(void)
{
    s_bpcount = 0;
    s_bp_reversed = true;
    s_kp_width_sum = 0;

    // Add an active node to start the paragraph
    {
        struct kp_node *n = malloc(sizeof(struct kp_node));
        n->pos = 0;
        n->score = 0;
        n->line = 0;
        n->w = 0;
        n->prev = NULL;
        n->link_n = NULL;
        n->link_p = NULL;

        s_kp_active.head = s_kp_active.tail = n;
    }

    // Reset inactive list
    s_kp_inactive.head = s_kp_inactive.tail = NULL;

    for (int i = 0; i < s_icount; ++i)
    {
        const struct lb_item *item = &s_items[i];

        switch(item->t)
        {
        case LB_BOX:
            s_kp_width_sum += item->w;
            break;
        case LB_GLUE:
            if (i > 0 &&
                s_items[i - 1].t == LB_BOX)
            {
                knuth_plass(item, i);
            }
            s_kp_width_sum += item->w;
            break;
        case LB_PENALTY:
            if (item->p.penalty == LB_INFINITY) break;
            knuth_plass(item, i);
            break;
        }
    }

    struct kp_node *n;
    if (!s_kp_active.head) goto no_head;

    // Find ideal breakpoint
    int best_score = LB_INFINITY;
    struct kp_node *best = NULL;
    for (n = s_kp_active.head; n; n = n->link_n)
    {
        if (n->score >= best_score) continue;

        best = n;
        best_score = best->score;
    }

    for (; best; best = best->prev)
    {
        ensure_breakpoint_buffer_incr();
        s_bp[s_bpcount++].pos = best->pos;
    }

    // Free the linked list
    for (n = s_kp_active.head; n;)
    {
        struct kp_node *tmp = n->link_n;
        free(n);
        n = tmp;
    }

    s_bp_cur = 1;

    // Free the inactive linked list
no_head:
    for (n = s_kp_inactive.head; n;)
    {
        struct kp_node *tmp = n->link_n;
        free(n);
        n = tmp;
    }
}

static void
knuth_plass(const struct lb_item *item, int item_index)
{
    struct kp_node *active = s_kp_active.head,
                   *next = NULL,
                   *best_node = NULL;
    int score,
        line,
        w,
        width_sum,
        badness,
        linelen;

    for (int best_score; active; )
    {
        best_score = LB_INFINITY;

        for (; active;)
        {
            score = 0;
            next = active->link_n;
            line = active->line + 1;

            w = s_kp_width_sum - active->w;

            if (item->t == LB_PENALTY) w += item->w;

            linelen = line == 1 ? s_linelen_initial : s_linelen_follow;

            // Line is too long
            if (w > linelen) goto next;

            // Last line doesn't contribute to score
            if (item_index == s_icount - 1) goto score_skip;

            // Calculate score
            badness = linelen - w;
            badness *= badness;
            if (item->t == LB_PENALTY && item->p.penalty > 0)
            {
                score = 1 + badness + item->p.penalty;
                score *= score;
            }
            else if (item->t == LB_PENALTY && item->p.penalty > -LB_INFINITY)
            {
                score = 1 + badness;
                score *= score;
                score -= item->p.penalty * item->p.penalty;
            }
            else
            {
                score = 1 + badness;
                score *= score;
            }

            // Factor in penalty for consecutive hyphens
            if (item->t == LB_PENALTY &&
                s_items[active->pos].t == LB_PENALTY)
            {
                score += TYPESET_LB_PENALTY_CONSECUTIVE_HYPHENS *
                    item->p.flag * s_items[active->pos].p.flag;
            }

            // Add total score
        score_skip:
            score += active->score;

            // Track node with best score
            if (score < best_score)
            {
                best_node = active;
                best_score = score;
            }

        next:
            if (item->t == LB_PENALTY && item->p.penalty == -LB_INFINITY)
            {
                // Delete active node on forced breaks
                if (active->link_p) active->link_p->link_n = active->link_n;
                else s_kp_active.head = active->link_n;

                if (active->link_n) active->link_n->link_p = active->link_p;
                else s_kp_active.tail = active->link_p;

                // Push the node to the linked list of the "unlinked" nodes so
                // we can free it later.
                if (!s_kp_inactive.head)
                {
                    active->link_p = NULL;
                    active->link_n = NULL;
                    s_kp_inactive.head = s_kp_inactive.tail = active;
                }
                else
                {
                    active->link_p = s_kp_inactive.tail;
                    active->link_n = NULL;
                    s_kp_inactive.tail->link_n = active;
                    s_kp_inactive.tail = active;
                }
            }

            active = next;
            if (active && active->line >= line) break;
        }

        // Calculate total sum
        width_sum = s_kp_width_sum;
        for (int x = item_index; x < s_icount; ++x)
        {
            const struct lb_item *x_i = &s_items[x];

            if (x_i->t == LB_BOX ||
                (x_i->t == LB_PENALTY &&
                 x_i->p.penalty == -LB_INFINITY &&
                 x > item_index)) break;

            if (x_i->t == LB_GLUE)
            {
                width_sum += x_i->w;
            }
        }

        if (best_score >= LB_INFINITY) continue;

        // Add node
        struct kp_node *n = malloc(sizeof(struct kp_node));
        n->pos = item_index;
        n->score = best_score;
        n->line = best_node->line + 1;
        n->w = width_sum;
        n->prev = best_node;

        if (active)
        {
            // Push node before the active one
            n->link_p = active->link_p;
            n->link_n = active;

            if (active->link_p) active->link_p->link_n = n;
            else s_kp_active.head = n;

            active->link_p = n;

            continue;
        }

        /* Push the node to end of the list */
        if (!s_kp_active.head)
        {
            n->link_p = NULL;
            n->link_n = NULL;
            s_kp_active.head = s_kp_active.tail = n;
            continue;
        }

        n->link_p = s_kp_active.tail;
        n->link_n = NULL;
        s_kp_active.tail->link_n = n;
        s_kp_active.tail = n;
    }
}

/* Returns true if given word may be the end of a sentence */
static bool
word_is_end_of_sentence(const char *s, size_t len)
{
    // Check if the last character of the word is a full-stop
    char last_char = s[len - 1];
    if (strchr(".!?", last_char) == NULL)
    {
        return false;
    }

    // Next tests assume the word ends with a full-stop
    if (last_char != '.') return true;

    // Next try work out if the word is in fact initials.  If it's less than
    // two characters then we assume it is.
    if (len == 2) return false;

    // If the second-last character is punctuation, e.g. the parenthesis in
    // "etc.).", then we need to skip the test below or else we get false
    // negatives like the above.
    if (!ispunct(s[len - 2]) && s[len - 2] != '.')
    {
        // Test for abbreviations like e.g. and i.e., by checking if more
        // full-stops are present in the word
        for (const char *s2 = s + len - 2; s2 >= s; --s2)
        {
            if (*s2 == '.') return false;
        }
    }

    return true;
}

static void
justify_text(struct lb_item *restrict first, struct lb_item *restrict last)
{
#if !TYPESET_JUSTIFY
    return;
#else

    // Last line is a special case; we don't want to actually justify it, but
    // we want to ensure that double-spaces after sentences are applied (if
    // TYPESET_FORCE_DOUBLE_SPACE_SENTENCE is set).
    bool
    is_last_line = s_bp_cur == s_bpcount - 1,
    is_first_line = (!s_bp_reversed && s_bp_cur == s_bpcount - 1) ||
        (s_bp_reversed && s_bp_cur == 0);

    // Count remaining and total spaces
    int space_remain = is_first_line ? s_linelen_initial : s_linelen_follow;
    int total_space_count = 0;
    for (struct lb_item *item = first; item <= last; ++item)
    {
        if (item->t != LB_PENALTY) space_remain -= item->w;
        if (item->t == LB_GLUE && !item->g.no_stretch) ++total_space_count;
    }

    // No spaces to justify
    if (total_space_count <= 0) return;

    // First iteration, we look for any sentence ends to firstly distribute the
    // space to, as these have the highest priority (so they have at least 2
    // spaces)
    for (struct lb_item *item = first; space_remain > 0 && item < last; ++item)
    {
        if (item->t != LB_BOX ||
            !item->b.content ||
            !item->b.w_canon ||
            !word_is_end_of_sentence(item->b.content, item->b.w_canon))
        {
            continue;
        }

        // Get following glue and increase space
        struct lb_item *glue;
        for (glue = item + 1;
            glue < last && (glue->t != LB_GLUE || glue->g.no_stretch);
            ++glue);
        if (glue >= last) continue;
        ++glue->w;
        --space_remain;
    }

    // No more spaces left
    if (space_remain <= 0) return;

    // Sentences now have double-spaces in last line.  Since we don't want to
    // justify the last line we return now
    if (is_last_line) return;

    /* Now distribute remaining spaces across the line */

    /*
     * New "right-left-right..." alternation method.  This seems to be a
     * pattern that nroff follows and it produces fantastic results in
     * comparison to the other attempted methods; despite this one being
     * literally one of the simplest methods.
     */
    bool alternate = (s_bp_cur + 1 + !s_bp_reversed) % 2;
    while (space_remain > 0)
    {
        if (alternate)
        {
            // Begin from start
            for (struct lb_item *item = first;
                space_remain > 0 && item < last;
                ++item)
            {
                if (item->t != LB_GLUE || item->g.no_stretch) continue;

                ++item->w;
                --space_remain;
            }
        }
        else
        {
            // Begin from end
            for (struct lb_item *item = last;
                space_remain > 0 && item > first;
                --item)
            {
                if (item->t != LB_GLUE || item->g.no_stretch) continue;

                ++item->w;
                --space_remain;
            }
        }
    }
#endif
}
