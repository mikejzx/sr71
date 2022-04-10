#ifndef LINE_BREAK_ALG_H
#define LINE_BREAK_ALG_H

/*
 * line_break_alg.h
 *
 * Line breaking algorithms
 */

struct lb_prepare_args
{
    // Line itself that we are to prepare
    const char *line, *line_end;

    // The length of the line we wish to produce
    unsigned length;

    // Bytes to skip at start of line
    unsigned offset;

    // Number of initial indentation characters (written to buffer)
    unsigned indent;

    // Number of "hanging" indentation characters (not actually written to
    // resulting string, but instead is factored into line widths)
    unsigned hang;
};

void line_break_init(void);
void line_break_deinit(void);

void line_break_prepare(const struct lb_prepare_args *);
ssize_t line_break_get(char *, size_t);
bool line_break_has_data(void);
void line_break_compute_greedy(void);
void line_break_compute_knuth_plass(void);

#endif
