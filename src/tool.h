#ifndef VANGOPIX_TOOL_H
#define VANGOPIX_TOOL_H

#include "vangopix.h"
#include "tabs.h"

/*
 * The tools, and the only file that changes a pixel.
 *
 * THE EIGHT KEYS ARE THE FIRST VANGOPIX'S OWN, and they are a 2x4 block under the left
 * hand with the right hand on the mouse - which is why they are these eight letters and
 * not the initials of the tool names:
 *
 *      Q  pencil     W  line       E  rect      R  ellipse
 *      A  eraser     S  bucket     D  spray     F  change-colours
 *
 * A KEY IS ONLY A TOOL KEY WHEN IT IS BARE. CTRL+S saves and S is the bucket; the two are
 * told apart by asking whether ANY modifier is down, not by asking about the one modifier
 * that happens to collide today. See core.c.
 *
 * EACH TOOL REMEMBERS ITS OWN SIZE, and SHIFT+wheel changes it. Per tool because an eraser
 * wants to be twenty pixels across and a pencil wants to be one, and having to re-dial the
 * size at every switch is what makes people stop switching.
 *
 * THE STEP IS PER TOOL TOO, and it is the detail worth carrying over: 1 for the pencil, the
 * line, the rectangle and the ellipse, 5 for the eraser, 3 for the spray and the
 * change-colours. The precise tools step one pixel at a time because a pixel is what they
 * are for; the broad ones would take twenty notches to get anywhere at that rate.
 *
 * HOW A TOOL SHOWS ITSELF, and none of it is a panel: the cursor is a crosshair over the
 * sheet, a line-art glyph hangs up and to the right of it saying which tool, and the tip is
 * outlined on the sheet so its size and shape can be seen before it is used.
 */

typedef enum {
	T_PENCIL = 0,
	T_LINE,
	T_RECT,
	T_ELLIPSE,
	T_ERASER,
	T_BUCKET,
	T_SPRAY,
	T_CHANGE,
	T_LOT
} TOOL;

/* Builds the cursors. Not fatal: without them the pointer keeps whatever shape the system
   gave it, and drawing works exactly the same. */
extern bool tool_init (void);
extern void tool_free (void);

/* Returns true when the tool consumed the event. */
extern bool tool_event (const SDL_Event *e, VNG_TAB *t);

/* Work measured in TIME rather than in events - the spray, which goes on spraying while the
   button is held and the hand is still. Called once a frame, before the sheet is drawn, so
   what it lays down appears in the same frame. */
extern void tool_frame (VNG_TAB *t);

/* The tip outline, the glyph, the hex readout and the two loaded colours. */
extern void tool_draw (VNG_TAB *t);

extern TOOL tool_current (void);
extern int  tool_tip_size (void);   /* of the current tool */

/*
 * TWO COLOURS, ONE PER MOUSE BUTTON. Slot 0 is the left button's, slot 1 the right
 * button's. Anything else reads as slot 0 rather than reading out of bounds.
 *
 * COLOUR 2 STARTS AS NOTHING, which is what makes the right button rub out without an
 * eraser being involved: in a program that keeps alpha, rubbing out IS drawing with
 * nothing. Put a colour in slot 2 and the right button draws with it.
 */
extern Uint32 tool_colour (int slot);

/*
 * CTRL+CLICK IS THE EYEDROPPER, AND THE PICK IS ON THE PRESS.
 *
 * Hover was tried and it is wrong: the hand rests, drifts and travels across the sheet on
 * its way somewhere, and a colour that changes under all of that is a colour nobody chose.
 * The first Vangopix asked for the press. Held and dragged, it goes on absorbing.
 *
 * The button that takes a colour is the button that lays it down, so there is nothing to
 * remember about which slot was filled.
 */
extern void tool_pick (VNG_TAB *t, int x, int y, int slot);

/*
 * A colour as a person reads it: RRGGBBAA, eight hex digits, no prefix.
 *
 * NOT the 0xAARRGGBB the code uses. The internal shape matches the ARGB8888 texture and
 * must stay that way; the shape on screen is the one that can be pasted into another editor
 * or a web tool, and it is the shape the first Vangopix wrote in its own config. The two
 * orders being different is exactly how the checkerboard once came out red, so the
 * conversion lives in one named place and its byte order is pinned by test/checks.c.
 */
extern void tool_hex (Uint32 argb, char *dst, size_t cap);

#endif
