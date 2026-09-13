#ifndef VANGOPIX_UI_H
#define VANGOPIX_UI_H

#include "vangopix.h"

/*
 * THE MEASUREMENTS THAT COME FROM THE FACE, AND THE REASON THEY HAVE TO.
 *
 * Every rectangle in this program's interface was ported from the first Vangopix, and the
 * first Vangopix drew text with a 6x6 BITMAP FONT scaled by an integer: vango_text_set(0,...)
 * was six pixels tall and vango_text_set(2,...) was eighteen. So its list rows, its buttons
 * and its form fields were sized around text six to eighteen pixels high.
 *
 * This program's face is TrueType with a SIXTEEN pixel line. The numbers came over; the text
 * they were holding tripled. That is the whole of why so much of this looks cramped, and it
 * is measurable rather than a matter of taste:
 *
 *     anim.c    EDIT_ROW 18   against a 16px line - two pixels of slack for seven stacked
 *                             form fields, so the labels all but touch
 *     anim.c    ROW      20   a list row that also carries two 20px hit boxes
 *     palette.c BTN_H    13   SHORTER THAN THE TEXT IN IT - which is why that button needed
 *                             a second, smaller font atlas and a block that grows to fit
 *     win.c     CLOSE_W  14   inside a head bar that is already line + 4 = 20, because
 *                             head_h() derives from the face and the box inside it did not
 *
 * That last one is the pattern in miniature: THE DERIVATION WAS ALREADY HAPPENING, in five
 * places - win.c's head, sidebar.c's rows, palette.c's button, tool.c's colour bars, anim.c's
 * labels - each measuring for itself. This finishes it and gives it one vocabulary, so that
 * changing the face changes every layout coherently instead of breaking a different thing
 * each time.
 *
 * WHAT IS *NOT* HERE IS THE POINT. Only things that HOLD TEXT derive from the face. Anything
 * that holds PIXELS keeps its number and always will:
 *
 *   - the palette's 24px swatch cell and its 6px checkerboard (which is the cell over four -
 *     scale it fractionally and the board stops dividing);
 *   - the two colour discs at radius 20, and the wheel's proportions, which are already
 *     fractions of a radius;
 *   - the sliders at 20 wide, the glyph's own -12..+10 grid, the 1:1 panel opening at 128
 *     because that is the size of a sprite.
 *
 * The asymmetry underneath is why: A FACE CAN BE RE-RASTERISED AT ANY SIZE AND STAY CRISP; a
 * one pixel hairline multiplied by 1.25 becomes 1.25 and either blurs or lands somewhere
 * nobody chose. It is the same argument that put integers on the zoom ladder in view.c.
 *
 * There is deliberately NO GLOBAL SCALE FACTOR yet. The face is the knob: raise
 * VNG_FONT_SIZE and everything below moves with it, which is what these calls are for.
 */

/* One line of the main face, and of the small one. Both answer sensibly with no font loaded -
   the checks run without one, and a layout that collapsed to zero there would be untestable. */
extern float ui_line  (void);
extern float ui_small (void);

/* The standard inset, a quarter of a line. Everything that says "a little space" says this. */
extern float ui_pad (void);

/* One character of the main face, measured on 'M'.
 *
 * ONLY MEANINGFUL BECAUSE THE SHIPPED FACE IS MONOSPACED, which is why it is the right way to
 * say "eight digits wide" or "this list should hold thirty characters" - a proportional face
 * makes every such promise drift. */
extern float ui_cell (void);

/* A row of text that can be clicked: a line with room above and below it. What a list row, a
   form field and a button all are. */
extern float ui_row (void);

/* A window's head bar, and the close box that sits in it. The box is square. */
extern float ui_head  (void);
extern float ui_close (void);

/* The stretch corner. Chrome rather than text, but it has to stay in proportion to the head
   it shares a window with, or a big window gets a handle that reads as a speck. */
extern float ui_grip (void);

/*
 * THE MARK THAT CLOSES A THING, DRAWN IN ONE PLACE BECAUSE IT IS IN FOUR.
 *
 * A window's head bar, a tab, a project folder in the sidebar and a clip in the animation
 * list all offer the same promise - press this and the thing goes away - and each of them was
 * drawing its own lowercase "x" with its own two colours and its own hand-placed offset. Four
 * copies of one idea is four places for it to drift, and it had already drifted: the tab's
 * dimmed to 0x909090 and the sidebar's to 0x707070 for no reason either could give.
 *
 * A RED DISC AND NOT A LETTER. An "x" is a character that happens to mean close; a red dot is
 * read before it is parsed, at any size, in any language, and it does not depend on the font
 * having loaded. It is sized off ui_close() so it stays in proportion to the head bar it most
 * often sits in, and centred in whatever box the caller hands it - which is what lets a 20x30
 * head box, a tab and a 30x30 list row all get the same mark.
 *
 * `hot` is the pointer being on it. The colour is red either way, because what it does does
 * not change when nobody is pointing at it.
 */
extern void ui_close_mark (SDL_FRect box, bool hot);

#endif
