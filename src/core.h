#ifndef VANGOPIX_CORE_H
#define VANGOPIX_CORE_H

#include "vangopix.h"

/* The frame. Everything that happens between two presents happens in here. */

extern void vangopix_input (void);   /* drain the event queue */
extern void vangopix_core  (void);   /* draw and present      */
extern void vangopix_core_free (void); /* the frame's own textures */

/* Tiles the desk into a rectangle. What anything that has to show NOTHING over the sheet
   needs - the selection's hole, so far. Exposed rather than copied because two checkerboards
   in one program would drift apart the first time one of the greys was tuned. */
/*
 * A TWO-TONE BOARD TILED INTO A RECTANGLE - the machinery, with the caller naming its tones.
 *
 * `square` is the side of one square, and a and b are 0xAARRGGBB. One tile per distinct board
 * is built once and kept, so this is a single draw call at any size; the naive way - a filled
 * rect per square - is about thirteen thousand calls a frame for the desk alone.
 *
 * WHAT MUST NOT FORK IS THE DESK, NOT EVERY BOARD. VNG_CHECK_A and VNG_CHECK_B live in this
 * header so that every place showing THE DESK shows the same desk: the sheet's transparency,
 * the selection's hole, the 1:1 panel, a swatch in the colour wheel. Two copies of those greys
 * drift apart the first time one is tuned. A board that is deliberately a DIFFERENT board is
 * not that - it is a second thing, and it says so by naming its own tones here.
 *
 * The palette's swatch grid is the one so far, and the first Vangopix chose its tones on
 * purpose: 0x303050 and 0x606080 against the desk's 0x202020 and 0x303030. Bluer, and its
 * light tone twice the desk's lightest.
 */
extern void vangopix_board_rect (SDL_FRect r, int square, Uint32 a, Uint32 b);

/* The desk itself: the board above in the two greys everything that shows THE DESK must use. */
extern void vangopix_desk_rect (SDL_FRect r);

/*
 * The same, under a CIRCLE, with `argb` laid over it at its real alpha and a rim in `rim` -
 * one the caller picks so it reads against the colour it just drew.
 *
 * BOTH ARE 0xAARRGGBB, and they are the same order on purpose. The rim was 0xRRGGBBAA at
 * first, because that is what everything drawn by the renderer takes here - text_print and
 * glyph_draw both do - while `argb` has to be 0xAARRGGBB, since it comes straight off a
 * colour slot and out of the document. TWO BYTE ORDERS IN ONE CALL is the trap this file
 * already warns about further down, and it caught the first check written against it: a rim
 * asked for as blue came out red, which is the exact failure recorded for the checkerboard.
 * One call, one order.
 *
 * It lives beside the tiled version for exactly the reason that one is exposed: SDL can clip
 * a tiled texture to a rectangle and to nothing else, so a round swatch has to step the
 * pattern out by hand - and two hand-stepped checkerboards in one program would drift apart
 * the first time one of the greys was tuned. The colour window's hue-ring hole was the first
 * caller; the palette's two loaded colours are the second.
 */
extern void vangopix_desk_disc (float cx, float cy, float r, Uint32 argb, Uint32 rim);

/*
 * The desk's own tile, for the shapes SDL cannot clip to - a circle, so far. Tiling handles
 * every rectangle; anything else has to lay the pattern down itself, and these are what it
 * must use so the two agree.
 *
 * 0xAARRGGBB, matching the ARGB8888 the tile texture is built in.
 */
/*
 * The zoom as a person reads it: a MULTIPLIER, which is the unit this program already thinks
 * in - "at 3x one art pixel is a 3x3 square", "1:1 where one art pixel is one screen pixel".
 *
 * In one named place, and pinned by test/checks.c, for the reason tool_hex is: it is shown in
 * two places - the corner readout and the F1 overlay - and two readouts of one number in
 * different units is a small trap that costs somebody an afternoon exactly once.
 */
extern void vangopix_zoom_text (float zoom, char *dst, size_t cap);

/*
 * HOW BIG THE FILE IS, in the unit a person would say it in: 512 B, 24.6 KB, 3.2 MB.
 *
 * K is 1024 and not 1000, which is the convention the file manager beside this program uses -
 * and the one that matters is that ONE of them is picked and written down, because a readout
 * that disagrees with Explorer about the same file is a readout nobody trusts twice.
 *
 * Bytes come out whole and everything above them gets one decimal. A second decimal on a
 * megabyte is four digits of noise about a number that is being glanced at.
 *
 * Its own named call for the same reason vangopix_zoom_text is one: a format with branches in
 * it, written twice, is a format that disagrees with itself the first time either copy is
 * touched. It is pinned in test/checks.c at the boundaries, where rounding decides whether
 * 1023.6 KB is a megabyte yet.
 */
extern void vangopix_size_text (Uint64 bytes, char *dst, size_t cap);

#define VNG_CHECK    6
#define VNG_CHECK_A  0xFF252525u
#define VNG_CHECK_B  0xFF303030u

#endif
