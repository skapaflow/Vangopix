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
 *      Z  select
 *
 * The select tool draws nothing: select.c owns the pointer while it is current, and comes
 * before this file in the chain.
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
	T_SELECT,
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

/* Puts a tool in hand from outside - what CTRL+V and CTRL+A call, since a paste with no way
   to move what was pasted is a gesture that stops halfway. */
extern void tool_set (TOOL t);
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

/* Puts a colour in a slot - what the colour window does, and what a palette would. */
extern void tool_set_colour (int slot, Uint32 argb);

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
 * One bucket drop, as one undo step. `barrier` picks between the two fills:
 *
 *   false  the ordinary bucket - spreads across ONE COLOUR and stops where it stops.
 *   true   the barrier fill - spreads across EVERYTHING and stops only where it meets the
 *          colour it is laying down. The tool for painting inside an outline you have just
 *          drawn, whatever is in there; a bucket refuses to cross a region of mixed shades,
 *          and the barrier does not care what it covers, only where the wall is.
 *
 * SHIFT chooses it at the moment of the press. Exposed rather than kept inside the event
 * handler because the difference between the two is a predicate, and a predicate is worth
 * pinning down in test/checks.c.
 */
extern void tool_fill (VNG_TAB *t, int x, int y, int slot, bool barrier);

/*
 * SHIFT SNAPS A LINE TO THE PIXEL-ART SLOPES: horizontal, 2:1, 1:1, 1:2, vertical, in twelve
 * sectors. The 2:1 is the isometric one - two across for every one down is the slope that
 * comes out CLEAN on a pixel grid, a run of two identical steps all the way, where an
 * arbitrary angle gives runs of 3, 2, 3, 2, 2 and reads as a wobble.
 *
 * (ax, ay) is the anchor and (x, y) the far end, moved in place. Takes the anchor as an
 * argument rather than reading the drag's, so the twelve sectors and their sign convention
 * can be checked without a mouse.
 */
extern void tool_snap_iso (int ax, int ay, int *x, int *y);

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

/*
 * THE POINTER'S SHAPE, AND ONE PLACE OWNS IT.
 *
 * The machine has exactly one cursor, so two modules setting it independently is two
 * SDL_SetCursor calls a frame disagreeing about the answer. This file already decides the
 * one shape that is always in play - a crosshair over the sheet, an arrow everywhere else -
 * so anything else that wants a shape while the pointer is over it ASKS here.
 *
 * The ask lasts ONE FRAME and the LAST asker wins, which is the draw order, which is the
 * stacking order, which is who the person is actually pointing at. Nobody asking means the
 * arrow. The first Vangopix had the same arrangement upside down: gui_cursor_set could be
 * called from anywhere and tool_cursor_mgr put the arrow back at the end of the frame if
 * none of SIX focus booleans was set - a list every new panel had to be added to. Here a
 * panel that stops asking simply stops being answered.
 *
 * It is also where its leak is fixed: gui_cursor_set called SDL_CreateSystemCursor on every
 * call and never freed the result, so a hand resting on a stretch band leaked a cursor per
 * frame. These are built once by tool_init and destroyed by tool_free.
 */
typedef enum {
	VNG_CUR_ARROW = 0,
	VNG_CUR_CROSS,
	VNG_CUR_WE,      /* west-east, for an edge that stretches sideways      */
	VNG_CUR_NS,      /* north-south, for one that stretches up and down     */
	VNG_CUR_NWSE,    /* the corner that does both, top-left to bottom-right  */
	VNG_CUR_NESW,    /* and the other diagonal, top-right to bottom-left     */
	VNG_CUR_LOT
} VNG_CURSOR;

extern void tool_cursor (VNG_CURSOR c);

/* Puts the frame's answer on screen and forgets it, so the next frame starts from the arrow
   again. Called once, last, by core.c - after everything that could have asked. */
extern void tool_cursor_apply (void);

/*
 * HOW A COLOUR IS DRAWN SO IT CAN BE SEEN, and these three are the whole of it.
 *
 * They are here rather than copied into every panel because the question is the same one in
 * three places - the two loaded slots at the bottom of the screen, the readout that follows
 * the pointer under CTRL, and the one under the palette summoned by ALT. A fourth copy of
 * these lines is how three answers stop agreeing, which is the same reason
 * vangopix_desk_rect is exposed from core.c.
 *
 *   tool_light_on   is WHITE what reads on top of this colour - by luminance over what is
 *                   behind it, never by inverting, which fails exactly at mid grey.
 *   tool_bar_draw   a colour with its hex written inside it, in whichever of the two reads.
 *   tool_bar_size   what such a bar must be to hold eight hex digits in the loaded face.
 *
 * EVERYTHING THAT SHOWS ALPHA SHOWS IT WITH A CHECKERBOARD, and these bars are no exception.
 * The sheet, the hole a floating selection leaves, the 1:1 panel, the disc in the colour
 * wheel and every cell of the palette all say "transparent" the same way, and a readout that
 * said it differently was making a person learn the word twice.
 *
 * These drew a SPLIT DOWN THE MIDDLE for a while - two flat tones, half each - on the
 * argument that a bar is wide and carries its hex inside it, where a board behind text is
 * noise. That was wrong twice over: the board only shows where the colour is TRANSPARENT, and
 * both its tones are dark, so the hex is white on dark either way.
 *
 * In the desk's own greys, because what must never fork is the desk. The palette's grid is
 * the one board that is deliberately a different board, and palette.c says why.
 */
extern bool tool_light_on (Uint32 argb);
extern void tool_bar_draw (SDL_FRect bar, Uint32 argb);
extern void tool_bar_size (float *w, float *h);

/*
 * Where the two loaded colours END on screen, so anything sitting beside them measures from
 * one place - the same reason sidebar_edge() exists.
 *
 * They already move: they step aside for the project panel, which is animated. Anything that
 * hardcoded its own copy of "two bars and a gap" would drift out of step with them the first
 * time either the face or that panel changed.
 */
extern float tool_slots_edge (void);

#endif
