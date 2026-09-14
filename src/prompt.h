#ifndef VANGOPIX_PROMPT_H
#define VANGOPIX_PROMPT_H

#include "vangopix.h"

/*
 * One line of text, asked for and gone. The first thing in the program that owned the
 * keyboard, and the reason keys.c exists rather than being designed on paper.
 *
 * IT IS NOT CHROME: closed, it draws nothing and costs one branch. It is the shape
 * section 1 asks for - a window that appears on demand and leaves.
 *
 * IT IS A VNG_FIELD, which is what this header used to promise it would become "the day this
 * program has a proper one-line field". It does, and the line is one: the caret, the arrows,
 * HOME, END, DELETE to empty it - and, the part that was a bug, it is closed by the same rule
 * that closes every box when another box opens. Its own private keyboard owner was invisible
 * to that rule, so a window's box clicked while it was up left its question on screen for good.
 *
 * It deliberately does not touch the mouse. Clicking a tab while a field is open still
 * switches tabs, and the field keeps the keyboard - a modal that also swallowed the
 * pointer would have to grey out the window behind it to be honest about it, and that is
 * a much bigger decision than one line of text needs to make.
 */

/* Handed the text on ENTER. Never called on ESC: cancelling is not an empty answer. */
typedef void (*PROMPT_DONE) (const char *text);

/*
 * Opens the field, HOLDING `initial` (which may be NULL) with the caret at its end - every box
 * in this program opens holding what it reads out, and DELETE is what empties one. See field.h.
 *
 * FALSE means it did not open, and the only reason is a missing font: a field that
 * cannot show what is being typed into it is a trap, so the caller keeps whatever it
 * would have done without asking. Every other module here degrades the same way -
 * text_draw on a NULL system is a no-op by design.
 */
extern bool prompt_open (const char *label, const char *initial, PROMPT_DONE done);

extern void prompt_draw (void);
extern void prompt_free (void);

/* Is the question up - what a check asks to see it go when another box takes the keyboard. */
extern bool prompt_up (void);

#endif
