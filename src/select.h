#ifndef VANGOPIX_SELECT_H
#define VANGOPIX_SELECT_H

#include "vangopix.h"
#include "tabs.h"

/*
 * THE SELECTION, AND IT IS TWO STATES AND NOT SEVEN.
 *
 * This was the hardest thing in the first Vangopix and it is worth saying why. Its
 * VANGO_SELECT carried seven booleans - set_selection, copy_img, drawing, above, drag, move,
 * cut - and a state machine written as flags is a state machine nobody can hold in their
 * head. Two of its bugs came straight out of that, and both are recorded here rather than
 * reproduced:
 *
 *   1. A DOUBLE FREE on every flip, rotate and invert. Each of those did
 *      `if (select->image) SDL_DestroyTexture(select->image);` without setting the pointer
 *      to NULL, and then called __update_texture__, which destroys it again - the more so
 *      because that function's own guard reads `if (image || *image)`, an || where the
 *      intent was &&, so it never actually checks the texture at all.
 *
 *   2. __update_texture__ REWROTE ITS SOURCE DATA, turning every pixel matching the
 *      background colour into transparency, destructively, on every texture refresh. Flip
 *      an image twice and it has eaten its own colour.
 *
 * WHAT REPLACES THE SEVEN FLAGS: a selection is MARKED or it is FLOATING, and the pixels
 * pointer is what says which.
 *
 *   marked    a rectangle is drawn on the sheet and the pixels are still in the document.
 *             `pixels` is NULL.
 *   floating  the pixels have been LIFTED into a buffer of their own and are being carried
 *             around. `pixels` holds them, `sx`/`sy` remember where they came from, and
 *             `cut` says whether that place is to be left empty.
 *
 * NOTHING IS WRITTEN TO THE DOCUMENT WHILE A SELECTION FLOATS. The hole is drawn rather
 * than dug, so there is no half-finished state on the sheet and nothing to clean up if the
 * float is abandoned. When it is dropped, ONE undo step clears the source and writes the
 * destination - which is also why a small move works: the two writes overlap, both carries
 * are recorded, and undo walks them backwards.
 *
 * THE CLIPBOARD IS ONE BUFFER FOR THE PROGRAM AND THE FLOAT IS ONE PER TAB, which is the
 * whole of the cross-tab story: copy in one document, switch, paste in another. The float
 * cannot leak between tabs because it is not shared, and it cannot outlive its tab because
 * it is freed with it.
 *
 * A float is a modal state that a person can SEE - the marching rectangle is on the sheet -
 * which is what makes it the one place bare keys are allowed to change meaning. V, H, I and
 * R transform the float, and they only do so while the select tool is current AND there is
 * something to transform. Otherwise R is still the ellipse.
 */

/* VNG_SEL is declared in tabs.h, which holds one per tab, and a second typedef of it here would
   not be legal C99 - select.c owns the struct; it is opaque everywhere else. */

extern void select_free (VNG_SEL *s);

/* Returns true when the selection consumed the event. Sits just before the tool in the
   chain: it owns the pointer while a float is being carried, and the drawing tools must not
   see those drags. */
extern bool select_event (const SDL_Event *e, VNG_TAB *t);

/* The marching rectangle, the hole, and the floating pixels. Drawn over the sheet and under
   the panels. */
extern void select_draw (VNG_TAB *t);

/*
 * THE EDGE EVERY COLOUR EDIT STAYS INSIDE. True with the marked rectangle in `r`, clamped to
 * the sheet - which can leave it EMPTY, when what is marked covers no pixel of the sheet (a
 * float put down out on the desk). False with `r` untouched when nothing is marked.
 *
 * While a float is in the air it is where the float is NOW, which is where it will land - but
 * a tool never draws under a float in practice: taking one puts the float down first.
 *
 * vng_tab_stroke_open is the caller that matters, and the reason this exists: see tabs.h.
 */
extern bool select_area (VNG_TAB *t, SDL_Rect *r);

/* Drops a float into the document as one undo step, or does nothing if there is none. What
   picking another tool calls, so that a float is never left hanging over a sheet a pencil is
   now drawing on. */
extern void select_commit (VNG_TAB *t);

/* The same, and any drag in progress is let go of too - a mark being pulled out, a float being
   carried. What vng_tab_settle calls: after it, nothing of the selection is half done. */
extern void select_settle (VNG_TAB *t);

/* Moves the marked rectangle, and a float with its hole, by (dx, dy) document pixels - what a
   canvas resize calls, since growing to the left moves every pixel of the drawing right. */
extern void select_shift (VNG_TAB *t, int dx, int dy);

/* Puts the clipboard down as a float centred on that document pixel, and takes the select
   tool: a paste with no way to move what was pasted is a gesture that stops halfway. CTRL+V
   calls it with the pointer's position; it takes one because "where" is the only thing the
   gesture adds, and because a keyboard cannot say it in a test. */
extern void select_paste (VNG_TAB *t, int x, int y);

extern void select_clipboard_free (void);

#endif
