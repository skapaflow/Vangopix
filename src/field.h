#ifndef VANGOPIX_FIELD_H
#define VANGOPIX_FIELD_H

#include "vangopix.h"

/*
 * ONE LINE OF TEXT INSIDE A WINDOW.
 *
 * This is the few widgets the frame was always going to need one day. `win.c` solves POSITION,
 * DRAG, STRETCH, Z-ORDER and who has the pointer, and deliberately not widgets - because no
 * window here was made of them: the colour wheel is a generated texture, the sliders are
 * one-channel ramps, the 1:1 panel is a viewport. The animation clip editor is the first
 * window that genuinely is a form - seven labelled boxes - and the answer to that was always
 * to grow the few widgets that shape needs here, against this renderer, rather than to take a
 * dependency.
 *
 * IT IS NOT NEW CODE. colour.c already had exactly one of these for its hex box, and this is
 * that, lifted out so there is one of them instead of eight. The proof it generalised is that
 * colour.c now uses it and its checks still pass.
 *
 * WHY A FIELD IS A THING AT ALL RATHER THAN A char[]: because typing has to take the keyboard
 * away from every shortcut in the program. Q is the pencil and TAB raises the sidebar, and
 * neither may fire while a name is being typed. keys.c settles that by OWNERSHIP - one holder,
 * offered every keyboard event before any layer - and a field is what holds it. That is the
 * whole reason this is not just a string.
 *
 * ONE FIELD IS OPEN AT A TIME, program-wide. Ownership does not stack in keys.c and it does
 * not stack here: opening a second field closes the first, keeping what was typed. That is
 * what makes a form work with no focus ring to manage - the open field IS the focus.
 */

typedef struct _vng_field_ VNG_FIELD;

/* What a field may contain. Longer than any caller needs today; a name is 31. */
#define VNG_FIELD_MAX 32

/* What a keystroke is allowed to be. A field that takes anything is how a frame count ends up
   holding a letter, and then strtof quietly answers zero. */
typedef enum {
	VNG_FIELD_TEXT = 0,   /* anything printable */
	VNG_FIELD_INT,        /* digits and a leading minus */
	VNG_FIELD_REAL,       /* digits, one dot, a leading minus */
	VNG_FIELD_HEX         /* hex digits, # and space - the colour window's box */
} VNG_FIELD_KIND;

/*
 * Makes a field. `text` is what it opens holding; it is never NULL and never longer than
 * VNG_FIELD_MAX. The field does not own it and does not write to it.
 *
 * `done` is called when ENTER commits, with the text as it stands. ESC and a press elsewhere
 * both close the field WITHOUT calling it - so escaping costs nothing and a mis-click does not
 * commit half a number.
 */
extern VNG_FIELD *field_make (VNG_FIELD_KIND kind, void (*done)(const char *, void *),
                              void *ctx);
extern void field_free (VNG_FIELD *f);

/*
 * Takes the keyboard, holding `text`.
 *
 * THE FIRST KEY TYPED REPLACES WHAT IS THERE AND THE REST ADD TO IT. The field has to open
 * holding its current value - or clicking one to read it would blank it - but the reason
 * anybody clicks is almost always to put something else in, and eight backspaces first is what
 * makes a field not worth using. BACKSPACE cancels the replace, because backspace means "I am
 * editing this one". A check found this: typing into a field already holding eight characters
 * appended nothing at all.
 */
extern void field_open (VNG_FIELD *f, const char *text);

/* Gives the keyboard back. `commit` says whether `done` is called. */
extern void field_close (VNG_FIELD *f, bool commit);

extern bool        field_open_p (VNG_FIELD *f);   /* is this one holding the keyboard */
extern const char *field_text   (VNG_FIELD *f);   /* what is in it right now */

/*
 * Draws it in `r`, with the caret when it is open. `label` may be NULL; when it is not, it is
 * drawn to the LEFT of the box in the small face and the box shrinks to fit beside it - which
 * is what makes seven of these stack into a form without a layout system.
 *
 * `show` is what to draw when the field is NOT open - so a box can read out a live value it
 * does not own, and become that value the moment it is typed into. Pass NULL to draw the
 * field's own text either way.
 */
extern void field_draw (VNG_FIELD *f, SDL_FRect r, const char *label, const char *show);

/*
 * Offered a press. True when it landed on the box and the field took the keyboard - which is
 * the answer a WIN_EVENT wants to return so the frame does not drag the window instead.
 *
 * A press ANYWHERE ELSE closes an open field without committing, and that is deliberate: a
 * click is not an ENTER. The window owner calls this for its own boxes and then, if none of
 * them took it, calls field_close on whichever is open.
 */
extern bool field_press (VNG_FIELD *f, SDL_FRect r, float x, float y);

#endif
