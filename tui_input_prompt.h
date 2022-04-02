#ifndef TUI_INPUT_PROMPT_H
#define TUI_INPUT_PROMPT_H

#include "tui_input.h"

/*
 * tui_input_prompt.h
 *
 * Handles TUI input prompts, as this sort of code can get seriously hideous
 */

void tui_input_prompt_begin(const char *, const char *, void(*)(void));
void tui_input_prompt_end(void);

// Actual input handlers
enum tui_status tui_input_prompt_text(const char *, ssize_t);
enum tui_status tui_input_prompt_register(const char *, ssize_t);

#endif
