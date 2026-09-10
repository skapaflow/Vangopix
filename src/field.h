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
 * Takes the keyboard, holding `text`, WITH THE CARET AT THE END OF IT.
 *
 * THE BOX IS EDITED, NOT REPLACED. Typing adds to what is there; it does not wipe it. That is
 * the first Vangopix's own arrangement - `*prompt = -1` on entry, resolved to the length of
 * the buffer, and every key inserted at the caret from there (print_input.c:31 and :44).
 *
 * IT DID NOT USED TO BE. The first key typed cleared the box, and the argument for it was that
 * eight backspaces before you can change a number is what makes a field not worth using. That
 * argument was TRUE AND IT WAS ABOUT THE MISSING CARET: with nowhere to stand in the string,
 * the only edit anybody could make was to retype the whole thing, so clearing on the first key
 * was the least bad of two bad answers. Now there is a caret, so a hand can go and change the
 * one digit it came to change, and clearing is just the box eating what was already right.
 *
 * WHAT MOVES THE CARET, and all of it is the original's:
 *
 *   LEFT RIGHT   one character, clamped at both ends (print_input.c:74-75)
 *   HOME END     the two ends, which nothing had before and every editor has
 *   BACKSPACE    takes the character BEFORE the caret, and the caret with it
 *   DELETE       EMPTIES THE BOX (print_input.c:61)
 *
 * That last one is not what DELETE means in a text editor, and it is right anyway - it is what
 * PAYS FOR the appending above. There is no select-all in this program, so without one key
 * that clears a box, changing #FF8000FF into something else is nine backspaces, which is the
 * friction that made clearing on the first keystroke look like a good idea in the first place.
 * Taking out the one character in front of the caret is the convenience; emptying the box is
 * the one a hand actually reaches for.
 */
extern void field_open (VNG_FIELD *f, const char *text);

/* Gives the keyboard back. `commit` says whether `done` is called. */
extern void field_close (VNG_FIELD *f, bool commit);

/*
 * WHAT TAB DOES, AND WHY IT IS NOT ANSWERED HERE.
 *
 * "The next box" is a fact about the FORM, not about the box: field.c holds one open field
 * program-wide and has no idea what is beside it, and giving it a list would be giving it the
 * layout system win.c was deliberately not given. So the owner says, and `dir` is all it is
 * told - +1 for TAB and -1 for SHIFT+TAB. The ctx it gets back is the field's own.
 *
 * This is the first Vangopix's gesture, from print_input.c:89 -
 *
 *     vinput->reg = key(K_LSHIFT) ? __max(reg-1, 0) : __min(reg+1, lot-1);
 *
 * - and those __max/__min are the half worth reading: the walk is CLAMPED AT BOTH ENDS AND
 * DOES NOT WRAP. TAB off the last box stays on the last box. A form of seven is a thing with a
 * top and a bottom, and a hand that finds the end wants to know it has found it, not to be put
 * back at the start.
 *
 * TAB LEAVES A BOX, AND LEAVING TAKES WHAT IS IN IT - the same rule a click into the next box
 * follows, so the two ways of crossing a form cannot disagree. The original got that for free
 * by writing into the caller's buffer as the keys arrived; here it is stated.
 *
 * A handler with nowhere to go OPENS NOTHING, and that is how the clamp is said: the box the
 * hand is in never closes, so it is still open and still holding the keyboard afterwards.
 *
 * A field with no step handler is a box ALONE - the colour window's hex readout - and TAB does
 * nothing in it, which is exactly what VNG_INPUT(1) clamped to.
 */
typedef void (*FIELD_STEP) (int dir, void *ctx);
extern void field_step (VNG_FIELD *f, FIELD_STEP fn);

/*
 * REPORTS AS IT IS TYPED: `done` is called on every keystroke, not only on ENTER.
 *
 * The first Vangopix did this to every box it had, by writing straight into the caller's
 * buffer as the keys arrived and rebuilding the model from all seven of them once a frame
 * (gui_animation.c:225, `/ * write settings * /`). It is the right behaviour where the answer
 * is drawn somewhere the eye already is - the animation clip is a RECTANGLE ON THE SHEET, and
 * typing a width with the grid moving under the hand is the difference between setting a clip
 * and guessing at one.
 *
 * It is per-keystroke and per-box rather than a rebuild of the whole form, which is the one
 * thing worth not carrying over: the original re-read all seven buffers every frame, so a
 * stray key in any box resized the clip that was playing.
 *
 * ESC STILL COSTS NOTHING. A live box remembers what it opened holding and reports that value
 * back on the way out, so cancelling puts the model where it was - which the original could
 * not do, having nothing but the buffer.
 */
extern void field_live (VNG_FIELD *f, bool on);

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
 * `show` IS THE SAME VALUE field_draw WAS GIVEN, and it is what the field opens holding. That
 * pairing is the whole contract: a box that reads out a live value it does not own has to
 * become that value when it is typed into, and it cannot if opening it starts from the field's
 * own buffer - which is empty until somebody has typed once. The animation editor's seven
 * boxes each read out the clip and each opened BLANK, so clicking one to change the width lost
 * the width. Pass NULL to open from the field's own text.
 *
 * A press ANYWHERE ELSE closes an open field without committing, and that is deliberate: a
 * click is not an ENTER. The window owner calls this for its own boxes and then, if none of
 * them took it, calls field_close on whichever is open - with `commit` being the OWNER's
 * decision, because a lone box and a form of seven mean different things by leaving one.
 */
extern bool field_press (VNG_FIELD *f, SDL_FRect r, float x, float y, const char *show);

#endif
