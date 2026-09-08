#ifndef VANGOPIX_PROMPT_H
#define VANGOPIX_PROMPT_H

#include "vangopix.h"

/*
 * One line of text, asked for and gone. The first thing in the program that owns the
 * keyboard, and the reason keys.c exists rather than being designed on paper.
 *
 * IT IS NOT CHROME: closed, it draws nothing and costs one branch. It is the shape
 * section 1 asks for - a window that appears on demand and leaves.
 *
 * PROVISIONAL, in the same sense the title bar is. The day this program has a proper one-line
 * field - the colour window's hex box is most of one already - this file is the one that
 * changes; what must NOT change is the keyboard contract underneath it, which is why that
 * lives in keys.c and not here.
 *
 * It deliberately does not touch the mouse. Clicking a tab while a field is open still
 * switches tabs, and the field keeps the keyboard - a modal that also swallowed the
 * pointer would have to grey out the window behind it to be honest about it, and that is
 * a much bigger decision than one line of text needs to make.
 */

/* Handed the text on ENTER. Never called on ESC: cancelling is not an empty answer. */
typedef void (*PROMPT_DONE) (const char *text);

/*
 * Opens the field. `initial` may be NULL, and is pre-filled ready to be typed over.
 *
 * FALSE means it did not open, and the only reason is a missing font: a field that
 * cannot show what is being typed into it is a trap, so the caller keeps whatever it
 * would have done without asking. Every other module here degrades the same way -
 * text_draw on a NULL system is a no-op by design.
 */
extern bool prompt_open (const char *label, const char *initial, PROMPT_DONE done);

extern void prompt_draw (void);

#endif
