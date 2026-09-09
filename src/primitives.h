#ifndef VANGOPIX_PRIMITIVES_H
#define VANGOPIX_PRIMITIVES_H

#include "vangopix.h"

/*
 * THE SHAPES SDL DOES NOT DRAW, AND THE ONES IT DRAWS LOOSELY.
 *
 * SDL gives a renderer points, lines and rectangles at FLOAT coordinates, and nothing round
 * at all. That is the right level for a renderer and the wrong one for an interface, because
 * an interface is made of hairlines: a 1px frame, an outline round the pixel under the
 * pointer, a rim on a colour swatch. Hand a float to a 1px line and where it lands is the
 * backend's rounding rule, not a decision anybody made.
 *
 * WHAT WENT WRONG WITHOUT THIS FILE, and it is the reason it exists. The colour discs were
 * drawn as three different circles at once: a fill from sqrt() with float ends that SDL
 * rounded its own way, a rim from SIXTY-FOUR straight chords off cos/sin, and a checkerboard
 * stepped between them. At radius 20 that rim is a ~126 pixel circumference cut into 2 pixel
 * chords whose vertices land between pixels and are rounded independently, so neighbouring
 * chords overdraw or leave a gap - and none of the three agreed with the other two. An
 * outline that misses its own fill by up to a pixel all the way round is what reads as a
 * badly resolved circle, and it is what the first Vangopix did NOT do: its circle() was the
 * midpoint algorithm, integer, eight-way symmetric, one pixel per step.
 *
 * SO THE RULE HERE IS ONE SHAPE, ONE SET OF NUMBERS. A disc, its outline and anything laid
 * inside it all come off prim_span - the same integer half-width per row - so they cannot
 * disagree. That is the whole idea; everything below is that idea spelled out.
 *
 * COLOURS ARE 0xAARRGGBB, matching the document, the colour slots and vangopix_desk_disc.
 * The two calls in this program that take 0xRRGGBBAA instead are text_print and glyph_draw,
 * which predate this; mixing the two orders in one call is exactly how a rim asked for as
 * blue came out red, and how the checkerboard once came out entirely red.
 */

/* ------------------------------------------------------------------ the circle's spine */

/*
 * A circle is measured in WHOLE PIXELS, because a circle centred on a half pixel is lopsided:
 * one side comes out a pixel fatter than the other, all the way round. The first Vangopix's
 * circle() took `int x0, int y0, int radius` and could not get this wrong; the float version
 * that replaced it never rounded and did.
 */
extern int prim_round (float v);

/*
 * The half-width of a disc of radius `ri` at row `dy`, in whole pixels - so the row spans
 * [cx - prim_span(ri, dy), cx + prim_span(ri, dy)] inclusive. Negative when the row is
 * outside the circle entirely.
 *
 * EXPOSED ON PURPOSE. Anything that has to lay something other than a flat colour inside a
 * circle - the desk's checkerboard behind a transparent swatch, which SDL cannot clip a tiled
 * texture to - walks the rows itself, and it must walk THESE rows or it is a second circle
 * that will drift from the first.
 */
extern int prim_span (int ri, int dy);

/* ----------------------------------------------------------------------- the shapes */

/* A filled disc. */
extern void prim_disc (float cx, float cy, float r, Uint32 argb);

/*
 * Its outline, one pixel thick, and it lands exactly on the edge of prim_disc drawn with the
 * same arguments.
 *
 * It is the outer disc LESS THE INNER one, row by row, which is what makes it closed by
 * construction: there is no chord to leave a gap between, and near the top and bottom - where
 * a circle's outline really is a long horizontal run - it comes out as one, instead of as a
 * chord pretending to be a curve.
 */
extern void prim_circle (float cx, float cy, float r, Uint32 argb);

/*
 * A line on whole pixels. Horizontal and vertical ones go down as rectangles, which is exact;
 * anything else is handed to SDL with its ends already rounded, so the rounding is this
 * program's and not the backend's.
 */
extern void prim_line (float x0, float y0, float x1, float y1, Uint32 argb);

/* A 1px rectangle outline, and a filled one, both snapped to whole pixels. */
extern void prim_rect (SDL_FRect r, Uint32 argb);
extern void prim_fill (SDL_FRect r, Uint32 argb);

/*
 * TWO NESTED OUTLINES, THE OUTER ONE A PIXEL PROUD OF THE INNER.
 *
 * The idiom this program uses wherever a mark has to be seen against an unknown background -
 * the outline round the pixel under the pointer, the 1:1 panel's region marker, the ring on a
 * chosen palette cell. One tone always disappears against something: the sheet can be any
 * colour and the desk behind it is grey. Two, offset by a pixel, cannot.
 */
extern void prim_box (SDL_FRect in, Uint32 inner, Uint32 outer);

#endif
