#ifndef VANGOPIX_GLYPH_H
#define VANGOPIX_GLYPH_H

#include "vangopix.h"

/*
 * The line-art glyphs, brought over from the first Vangopix unchanged.
 *
 * WHY LINES AND NOT AN IMAGE. A wireframe drawn from a handful of points scales to any
 * size from one definition, costs no file, and - the reason that matters here - it does
 * NOT OCCLUDE. A filled icon under the hand hides the artwork it is standing on; an
 * outline lets the colours through the middle of itself. That is what makes a tool glyph
 * usable as part of a cursor at all, and it is the author's answer, not a new one.
 *
 * EACH GLYPH IS ONE CLOSED PATH THAT REVISITS ITS OWN VERTICES. `vpencil` walks out to the
 * tip, back over itself, and on to the next edge; the repeated points are how a single
 * polyline draws a shape that is not a simple loop. It looks like a mistake in the data and
 * it is the technique.
 *
 * THE BLACK COPY AT +1,+1 IS NOT DECORATION. A white outline over white artwork is
 * invisible, and a black one vanishes over black. Drawing the path twice - offset by one
 * pixel in black, then in colour on top - gives every line an edge to be seen against,
 * whatever is underneath. The first Vangopix does exactly this in draw_wireframe_entity.
 */

/* IN TOOL ORDER, so a tool is its own glyph index and there is no table in between. Every
 * shape is the first Vangopix's, vertex for vertex, from its src/gui/gui_main_tool.c. */
typedef enum {
	GLYPH_PENCIL = 0,
	GLYPH_LINE,
	GLYPH_RECT,
	GLYPH_ELLIPSE,
	GLYPH_ERASER,
	GLYPH_BUCKET,
	GLYPH_SPRAY,
	GLYPH_CHANGE,
	GLYPH_SELECT,
	GLYPH_PICK,
	GLYPH_POINTER,
	GLYPH_LOT
} GLYPH;

/*
 * Draws the glyph centred on (x, y), turned `rot` degrees, scaled, in `rgba` (0xRRGGBBAA)
 * with its own shadow. A glyph is centred on its origin, so (x, y) is where it hangs from -
 * see tool.c for the offset that keeps it clear of the pixel being aimed at.
 *
 * The rotation is what makes GLYPH_POINTER worth having: one arrow, turned to whatever it is
 * pointing at, is how the colour wheel marks a hue and how a slider marks its level. The
 * first Vangopix's draw_wireframe_entity took the same parameter for the same reason.
 */
extern void glyph_draw (GLYPH g, float x, float y, float rot, float scale, Uint32 rgba);

#endif
