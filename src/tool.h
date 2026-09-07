#ifndef VANGOPIX_TOOL_H
#define VANGOPIX_TOOL_H

#include "vangopix.h"
#include "tabs.h"

/*
 * The pencil, and the first thing in this program that changes a pixel.
 *
 * IT IS ALWAYS THE ACTIVE TOOL, AND THERE IS NO KEY TO SELECT IT. With one tool, a key
 * that selects it does nothing, and a mode nobody can leave is not a mode. The letter
 * shortcuts arrive with the second tool, which is also when they start teaching anything.
 *
 * HOW IT SHOWS ITSELF, WHICH COSTS NO CHROME AT ALL:
 *
 *   - the CURSOR is a crosshair over the sheet and an arrow everywhere else. The shape is
 *     the mode indicator, and it lives outside the window's pixels entirely - the system
 *     draws it, at the size and theme the person configured.
 *   - the PIXEL UNDER THE POINTER is outlined on the sheet, which is the signal that
 *     actually matters in a pixel editor: it says which pixel, not which tool.
 *
 * ONE GLYPH CAN SERVE BOTH THE PALETTE AND THE POINTER, and the objection to it was
 * answered before it was raised. It looks like it cannot: a tool icon under the hand
 * covers the pixel being aimed at. The first Vangopix solved that twice over - the glyph
 * sat a few pixels OFF the hot spot, and being drawn as LINES rather than as a filled
 * shape it never hid the colours underneath. Both halves are recorded here because they
 * are the whole reason the idea works, and because the readout below follows the same
 * rule: whatever reports what you are pointing at must not stand on it.
 *
 * The crosshair is what is here today. A line-art glyph per tool is the next step, and it
 * is SDL_CreateColorCursor with the hot spot at the aim point - not a redesign.
 *
 * TWO COLOURS, ONE PER MOUSE BUTTON, AND CTRL+CLICK FILLS THEM.
 *
 * Left button draws colour 1, right button draws colour 2. CTRL held turns both buttons
 * into the eyedropper: CTRL+left absorbs into 1, CTRL+right into 2. The button that takes
 * a colour is the button that will lay it down, so there is nothing to remember about
 * which slot was filled.
 *
 * COLOUR 2 STARTS TRANSPARENT, which is what makes the right button an eraser without an
 * eraser existing. It is not a special case bolted on - in a program that keeps alpha,
 * rubbing out IS drawing with nothing, and the secondary colour is where nothing lives
 * until somebody puts something there.
 *
 * THE PICK IS ON THE PRESS, not on hover. Hovering was tried and it is wrong: the hand
 * rests, drifts and travels across the sheet on its way to somewhere, and a colour that
 * changes under all of that is a colour nobody chose. The first Vangopix asked for the
 * press and it was right to.
 *
 * What hover does instead is PREVIEW. While CTRL is held a bar follows the pointer with
 * the colour under it and its value as RRGGBBAA - the answer to "what would I get", which
 * is what makes a deliberate press worth making. It sits OFFSET from the aim point, for
 * the same reason the first Vangopix offset its tool glyph: whatever reports what you are
 * pointing at must not stand on it.
 *
 * The two loaded colours are shown in the same kind of bar, at the bottom left, and they
 * step aside when the project sidebar comes in.
 *
 * THEY ARE ALWAYS THERE, AND THAT IS A READOUT RATHER THAN CHROME. A toolbar is commands
 * parked on screen in case they are wanted; a swatch answers "which colour lands if I press
 * the left MOUSE button now" - the state of the thing in your hand, not an entry in a menu.
 * There is no other way to know what each side of the mouse is holding, and that is not a
 * question a person should have to press a key to ask.
 *
 * A pixel artist settles on eight to sixteen colours and picks from the drawing constantly -
 * THE IMAGE IS THE PALETTE - so what has to be on screen is not a grid of tiles but the two
 * colours currently in hand.
 *
 * IT IS LAST IN THE INPUT CHAIN, after everything that can claim the same button - the
 * bar, the panel, the corner grips and the camera's space-pan. That order is in core.c
 * and it is load-bearing.
 */

/* Builds the two cursors. Not fatal: without them the pointer keeps whatever shape the
   system gave it, and drawing works exactly the same. */
extern bool tool_init (void);
extern void tool_free (void);

/* Returns true when the tool consumed the event. */
extern bool tool_event (const SDL_Event *e, VNG_TAB *t);

/* Slot 0 is the left button's colour, slot 1 the right button's. Anything else reads as
   slot 0 rather than reading out of bounds. The palette, when it exists, works here. */
extern Uint32 tool_colour (int slot);

/* Absorbs the colour at that document pixel into a slot. Out of bounds changes nothing. */
extern void tool_pick (VNG_TAB *t, int x, int y, int slot);

/*
 * A colour as a person reads it: RRGGBBAA, eight hex digits, no prefix.
 *
 * NOT the 0xAARRGGBB the code uses. The internal shape matches the ARGB8888 texture and
 * must stay that way; the shape on screen is the one that can be pasted into another
 * editor or a web tool, and it is the shape the first Vangopix wrote in its own config.
 * The two orders being different is exactly how the checkerboard once came out red, so the
 * conversion lives in one named place and its byte order is pinned by test/checks.c.
 */
extern void tool_hex (Uint32 argb, char *dst, size_t cap);

/* The pixel outline, and the cursor shape for where the pointer is now. */
extern void tool_draw (VNG_TAB *t);

#endif
