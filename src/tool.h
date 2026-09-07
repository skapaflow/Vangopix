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
 * CTRL IS THE EYEDROPPER, AND IT ABSORBS ON HOVER - NO CLICK.
 *
 * Hold CTRL over the sheet and the pencil's colour becomes the pixel under the pointer,
 * live, following the hand until CTRL is let go. That is not a shortcut for a click; it is
 * the shape of the work. A pixel artist settles on eight to sixteen colours and then picks
 * from the drawing constantly - which means THE IMAGE IS THE PALETTE, and a strip of
 * coloured tiles down the side of the screen is answering a question that the sheet
 * already answers. A palette window will still come, for the times a colour is not on the
 * sheet yet, and it will be summoned like everything else here.
 *
 * While CTRL is held a hex readout follows the pointer: a bar filled with the colour, the
 * value inside it as RRGGBBAA. It sits OFFSET from the aim point, for the same reason the
 * first Vangopix offset its tool glyph - whatever reports what you are pointing at must
 * not stand on it.
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

/* What the pencil lays down. The palette, when it exists, sets and reads it here. */
extern Uint32 tool_colour (void);

/* Absorbs the colour at that document pixel. Out of bounds changes nothing. */
extern void tool_pick (VNG_TAB *t, int x, int y);

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
