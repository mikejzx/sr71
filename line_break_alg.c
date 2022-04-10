#include "pch.h"
#include "line_break_alg.h"
#include "hyphenate_alg.h"

#define LINEBREAK_INITIAL_ITEM_COUNT 16384
#define LINEBREAK_INITIAL_BREAKPOINT_COUNT 16384

// Whether to justify text to the margins
#define TYPESET_JUSTIFY 1

// Force double-space sentences in non-justified text (disabled in justified to
// hopefully avoid too many spaces; however end-of-sentences are prioritised
// higher by justification algorithm)
// TODO: force double spaces in last (non-justified) line of paragraph
#if TYPESET_JUSTIFY
#  define TYPESET_FORCE_DOUBLE_SPACE_SENTENCE 0
#else
#  define TYPESET_FORCE_DOUBLE_SPACE_SENTENCE 1
#endif

#define LB_INFINITY 99999

// Represents a single "item" in a string of text.  All algorithms are based
// around the fundamental 'box-glue-penalty' model as used in Knuth-Plass
struct lb_item
{
    enum lb_item_type
    {
        LB_BOX,
        LB_GLUE,
        LB_PENALTY,
    } t;

    // All items types store a width
    uint8_t w;

    union
    {
        // Fixed-width item that can be typeset within the paragraph.
        // Usually we use boxes to store word fragments
        struct lb_box
        {
            // Content of the box; the number of chars will be the width of the
            // box
            const char *content;

            // The true width (in bytes) of the content to be displayed.  E.g.
            // UTF-8 text may have more bytes here, while having a shorter 'w'
            // width
            uint8_t w_canon;
        } b;

        // Glue is used to "stick the boxes together" and are what essentially
        // define the space.  We have no reason to use stretch/shrink
        // properties as defined in Knuth-Plass, as our implementation does not
        // need to support proportional fonts
        //struct lb_glue { } g;

        // Penalties can be used to define breakpoints within the paragraph;
        // (e.g. hyphenation points) though have a number associated with them
        // to indicate the desirability of that break
        struct lb_penalty
        {
            int penalty;
            bool flag;
        } p;
    };
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

    // Width of the line itself
    int line_w;

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
    int pos;
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
static struct kp_ll s_kp_active;
static int s_kp_width_sum;
static void knuth_plass(const struct lb_item *, int);

// Misc
static int s_linelen;
static bool word_is_end_of_sentence(const char *, size_t);
static void justify_text(struct lb_item *restrict, struct lb_item *restrict);

void
line_break_init(void)
{
    // TODO dynamically resize capacity and breakpoints

    s_icount = 0;
    s_icap = LINEBREAK_INITIAL_ITEM_COUNT;
    s_items = malloc(s_icap * sizeof(struct lb_item));

    s_bpcap = LINEBREAK_INITIAL_BREAKPOINT_COUNT;
    s_bp = malloc(s_bpcap * sizeof(struct lb_breakpoint));

    memset(&s_kp_active, 0, sizeof(struct kp_ll));
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

    s_linelen = args->length;

    // Resize buffer if needed
    // line_break_ensure_buffer((args->line->bytes * 3) / 2);

    const char
        *c_last = args->line + args->offset,
        *c = c_last;

    // Move over leading whitespace
    for (c_last = c; *c_last == ' ' || *c_last == '\t'; ++c_last);

    // Add indent box
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

        // Find the hyphenation points in the word, and add them as penalty
        // items between the boxes
        hyphenate(c_last, c - c_last);
        int h_last = 0;
        for (int h = hyphenate_get(); h != -1; h = hyphenate_get())
        {
            s_items[s_icount++] = (struct lb_item)
            {
                .t = LB_BOX,
                .w = utf8_strnlen_w_formats(c_last + h_last, h - h_last),
                .b =
                {
                    .content = c_last + h_last,
                    .w_canon = h - h_last,
                }
            };
            s_items[s_icount++] = (struct lb_item)
            {
                .t = LB_PENALTY,
                .w = 1,
                .p = { .penalty = 20, .flag = true }
            };
            h_last = h;
        }

        s_items[s_icount++] = (struct lb_item)
        {
            .t = LB_BOX,
            .w = utf8_strnlen_w_formats(c_last + h_last, (c - c_last) - h_last),
            .b =
            {
                .content = c_last + h_last,
                .w_canon = (c - c_last) - h_last,
            }
        };

        // Explicit hyphens/dashes get followed by a flagged penalty item with
        // width zero
        if (hyphen_count)
        {
            s_items[s_icount++] = (struct lb_item)
            {
                .t = LB_BOX,
                .w = 1,
                .b =
                {
                    .content = "-",
                    .w_canon = 1,
                },
            };
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

            s_items[s_icount++] = (struct lb_item)
            {
                .t = LB_GLUE,
            #if TYPESET_FORCE_DOUBLE_SPACE_SENTENCE
                .w = end_of_sentence ? 2 :
            #endif
                1,
            };

            // Add penalty "bonus" after sentences to encourage sentences to
            // begin at start of line instead of end
            if (end_of_sentence)
            {
                s_items[s_icount++] = (struct lb_item)
                {
                    .t = LB_PENALTY,
                    .p =
                    {
                        .penalty = -15
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
    s_items[s_icount++] = (struct lb_item)
    {
        .t = LB_GLUE,
        .w = 0,
    };
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

    // Now draw the items out
    char *buf_ptr = buf;
    for (const struct lb_item *item = first; item <= last; ++item)
    {
        switch(item->t)
        {
        case LB_BOX:
            if (item->b.content == NULL)
            {
                // Write spaces for empty boxes (e.g. indent)
                buf_ptr += snprintf(
                    buf_ptr,
                    buf_len - (buf_ptr - buf),
                    "%*s", item->w, "");
                break;
            }
            buf_ptr += snprintf(
                buf_ptr,
                buf_len - (buf_ptr - buf),
                "%.*s", item->b.w_canon, item->b.content);
            break;
        case LB_GLUE:
            if (item == first || item == last) continue;

            buf_ptr += snprintf(
                buf_ptr,
                buf_len - (buf_ptr - buf),
                "%*s",
                item->w,
                "");
            break;
        case LB_PENALTY:
            if (item != last || !item->w) continue;
            buf_ptr += snprintf(
                buf_ptr,
                buf_len - (buf_ptr - buf),
                "-");
            break;
        default: break;
        }
    }

    ++s_bp_cur;
    return buf_ptr - buf;
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
            ++s_bpcount;
            w = 0;
            last_box = NULL;
            continue;
        }

        if (item->t != LB_BOX) continue;

        if (w + item->w >= s_linelen)
        {
            s_bp[s_bpcount].pos = (last_box ? (last_box - s_items) : i) + 1;

            ++s_bpcount;
            w = 0;
            last_box = NULL;
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

    if (!s_kp_active.head) return;

    // Find ideal breakpoint
    int best_score = LB_INFINITY;
    struct kp_node *best = NULL;
    for (struct kp_node *n = s_kp_active.head; n; n = n->link_n)
    {
        if (n->score >= best_score) continue;

        best = n;
        best_score = best->score;
    }

    for (; best; best = best->prev)
    {
        s_bp[s_bpcount++].pos = best->pos;
    }

    // Free the linked list
    for (struct kp_node *n = s_kp_active.head; n != NULL;)
    {
        struct kp_node *tmp = n->link_n;
        free(n);
        n = tmp;
    }

    s_bp_cur = 1;
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
        badness;

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

            // Line is too long
            if (w > s_linelen) goto next;

            // Last line doesn't contribute to score
            if (item_index == s_icount - 1) goto score_skip;

            // Calculate score
            badness = s_linelen - w;
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
                score += 100 * item->p.flag * s_items[active->pos].p.flag;
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

                //free(active);
                //active = NULL;
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

    // Finally test for abbreviations like e.g. and i.e., by checking if more
    // full-stops are present in the word
    for (const char *s2 = s + len - 2; s2 >= s; --s2)
    {
        if (*s2 == '.') return false;
    }

    return true;
}

static void
justify_text(struct lb_item *restrict first, struct lb_item *restrict last)
{
#if !TYPESET_JUSTIFY
    return;
#else

    int space_remain;
    if ((!s_bp_reversed && s_bp_cur == 0) ||
        (s_bp_reversed && s_bp_cur == s_bpcount - 1))
    {
        // Don't justify last line
        space_remain = 0;
    }
    else
    {
        space_remain = s_linelen;
        for (struct lb_item *item = first; item <= last; ++item)
        {
            if (item->t != LB_PENALTY) space_remain -= item->w;
        }

        // Hanging punctuation
        if (ispunct(last->b.content[last->b.w_canon - 1]))
        {
            ++space_remain;
        }
    }

    int total_space_count = 0;
    for (struct lb_item *item = first; item < last; ++item)
    {
        if (item->t == LB_GLUE) ++total_space_count;
    }

    // No spaces to justify
    if (total_space_count <= 0) return;

#if 0
    /*
     * Algorithm B: attempts to evenly distribute the remaining space across
     *              whatever spaces are in the line.  Doesn't look great, but
     *              yeah...
     */
    if (total_space_count > 0 && space_remain > 0)
    {
        // Number of spaces we can distribute to all spaces in the line
        int spaces_even_total = space_remain - space_remain % total_space_count;

        float remainder_incr =
            ((float)total_space_count + spaces_even_total) / space_remain;

        if (remainder_incr > 0.0f)
        {
            int index_prev = -1, index;

            for (float incr = 0.0f;
                incr < (float)total_space_count && space_remain > 0;
                incr += remainder_incr)
            {
                index = (int)ceil(incr);
                if (index != index_prev)
                {
                    int i = 0;
                    for (struct lb_item *item = first; item < last; ++item)
                    {
                        if (item->t != LB_GLUE) continue;

                        if (i == index)
                        {
                            ++item->w;
                            --space_remain;
                            break;
                        }
                        ++i;
                    }

                    index_prev = index;
                }
            }
        }
    }
#else
    /*
     * Algorithm C: uses a fairly basic scoring system to find the theoretically
     *              more desirable places to put spaces.
     */
    int best_rank, rank;
    struct lb_item *best_space, *box;
    while (space_remain > 0)
    {
        // Iterate over the line, and find best space to use
        best_rank = LB_INFINITY;
        best_space = NULL;
        for (struct lb_item *item = first; item < last; ++item)
        {
            if (item->t != LB_GLUE) continue;

            /* Determine rank of space */

            // Raise the current space to an exponent to avoid using same space
            // too many times
            rank = item->w * item->w;
            rank *= rank;
            rank *= rank;

            int len_left = 0, len_right = 0;

            // Consider boxes around the space
            if (item > first)
            {
                for (box = item - 1; box >= first && box->t != LB_BOX; --box);
                if (box->t == LB_BOX)
                {
                    len_left = box->w * box->w;

                    // Prefer if at end of sentence or if punctuation
                    bool eos = word_is_end_of_sentence(
                        box->b.content,
                        box->b.w_canon);
                    rank -= eos * 50;
                    if (!eos && ispunct(box->b.content[box->b.w_canon - 1]))
                    {
                        rank -= 2;
                    }
                }
            }
            if (item < last)
            {
                for (box = item + 1; box <= last && box->t != LB_BOX; ++box);
                if (box->t == LB_BOX)
                {
                    len_right = box->w * box->w;

                    // Prefer if at end of sentence or if punctuation
                    bool eos = word_is_end_of_sentence(
                        box->b.content,
                        box->b.w_canon);
                    rank -= eos * 50;
                    if (!eos && ispunct(box->b.content[0]))
                    {
                        rank -= 2;
                    }
                }
            }

            //rank += sqrt(len_left * len_left + len_right * len_right);
            rank += len_left + len_right;

            // Check if rank is better than what we've already got
            if (rank < best_rank)
            {
                best_rank = rank;
                best_space = item;
            }
        }

        // Apply space
        if (best_space)
        {
            ++best_space->w;
            --space_remain;
        }
    }
#endif

#endif // TYPESET_JUSTIFY
}