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
 * That pair is why the first Vangopix's idea of one icon serving both the tool window and
 * the pointer does not survive here. The two jobs diverge: a palette button needs
 * IDENTITY (which tool is this), a pointer needs PRECISION (where exactly am I), and a
 * pencil glyph following the mouse covers the very pixel it is aiming at.
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

/* The pixel outline, and the cursor shape for where the pointer is now. */
extern void tool_draw (VNG_TAB *t);

#endif
