#include "primitives.h"

/*
 * Unpacks 0xAARRGGBB straight onto the renderer. One place, so no call site has to remember
 * the byte order - which is the mistake this file's header warns about.
 *
 * AND IT TURNS BLENDING ON, because a colour with an alpha channel that is not blended is a
 * colour whose alpha was a lie. Nothing in this program sets the draw blend mode globally:
 * seven modules set it themselves before drawing something transparent, which means anything
 * that does NOT set it is reading whatever the last of those left behind - and the answer
 * changes the day somebody reorders the frame. A primitive that accepts an alpha owes its
 * caller the blend, so it is stated here rather than assumed there.
 */
static void use (Uint32 argb)
{
	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(vng_ren, (Uint8)((argb >> 16) & 0xFF), (Uint8)((argb >> 8) & 0xFF),
	                                (Uint8)( argb        & 0xFF), (Uint8)((argb >> 24) & 0xFF));
}

int prim_round (float v) { return (int) SDL_floorf(v + 0.5f); }

int prim_span (int ri, int dy)
{
	if (ri < 0 || dy > ri || dy < -ri) return -1;
	return prim_round(SDL_sqrtf((float)(ri * ri - dy * dy)));
}

/* One row of a shape, as whole pixels. Every span in this file goes down through here, so
   "whole pixels" is stated once rather than at twenty call sites. */
static void row (int x, int y, int w)
{
	SDL_FRect r;

	if (w <= 0) return;

	r.x = (float)x;
	r.y = (float)y;
	r.w = (float)w;
	r.h = 1.0f;
	SDL_RenderFillRect(vng_ren, &r);
}

void prim_disc (float fcx, float fcy, float fr, Uint32 argb)
{
	int ri = prim_round(fr);
	if (ri < 1) return;

	int cx = prim_round(fcx), cy = prim_round(fcy);

	use(argb);

	for (int dy = -ri; dy <= ri; dy++) {
		int h = prim_span(ri, dy);
		row(cx - h, cy + dy, h * 2 + 1);
	}
}

void prim_circle (float fcx, float fcy, float fr, Uint32 argb)
{
	int ri = prim_round(fr);
	if (ri < 1) return;

	int cx = prim_round(fcx), cy = prim_round(fcy);
	int in = ri - 1;

	use(argb);

	for (int dy = -ri; dy <= ri; dy++) {
		int ho  = prim_span(ri, dy);
		int hin = prim_span(in, dy);

		/*
		 * The outer disc less the inner one. Where the inner circle does not reach - the very
		 * top and bottom rows - the whole run is outline, which is correct: that IS what the
		 * outline of a circle looks like there, and drawing a chord instead is what made the
		 * old version read as a polygon.
		 */
		if (hin < 0) {
			row(cx - ho, cy + dy, ho * 2 + 1);
		} else {
			row(cx - ho,      cy + dy, ho - hin);
			row(cx + hin + 1, cy + dy, ho - hin);
		}
	}
}

void prim_line (float x0, float y0, float x1, float y1, Uint32 argb)
{
	int ax = prim_round(x0), ay = prim_round(y0);
	int bx = prim_round(x1), by = prim_round(y1);

	use(argb);

	/* The two that an interface is actually made of, and the two SDL is loosest about: a
	 * horizontal or vertical hairline is a rectangle, which lands where it is put. */
	if (ay == by) { row(ax < bx ? ax : bx, ay, (ax < bx ? bx - ax : ax - bx) + 1); return; }

	if (ax == bx) {
		SDL_FRect r = { (float)ax, (float)(ay < by ? ay : by), 1.0f,
		                (float)((ay < by ? by - ay : ay - by) + 1) };
		SDL_RenderFillRect(vng_ren, &r);
		return;
	}

	/* Anything else is SDL's Bresenham, with the ends already rounded here so the rounding is
	 * this program's decision rather than the backend's. */
	SDL_RenderLine(vng_ren, (float)ax, (float)ay, (float)bx, (float)by);
}

/* Snapped to whole pixels, and the size is taken from the SNAPPED edges rather than rounded
   on its own - or a rectangle from x=10.6 to x=20.6 would come out 10 wide starting at 11,
   ending one short of where it was asked to end. */
static SDL_Rect whole (SDL_FRect r)
{
	int x0 = prim_round(r.x),          y0 = prim_round(r.y);
	int x1 = prim_round(r.x + r.w),    y1 = prim_round(r.y + r.h);

	SDL_Rect o = { x0, y0, x1 - x0, y1 - y0 };
	return o;
}

void prim_fill (SDL_FRect r, Uint32 argb)
{
	SDL_Rect  w = whole(r);
	SDL_FRect f = { (float)w.x, (float)w.y, (float)w.w, (float)w.h };

	if (w.w <= 0 || w.h <= 0) return;

	use(argb);
	SDL_RenderFillRect(vng_ren, &f);
}

void prim_rect (SDL_FRect r, Uint32 argb)
{
	SDL_Rect w = whole(r);

	if (w.w <= 0 || w.h <= 0) return;

	use(argb);

	/* Four rectangles and not SDL_RenderRect: the corners are then written once each, which
	 * matters the moment the colour is not opaque - a rect drawn as four lines darkens its
	 * own corners where they overlap. */
	row(w.x, w.y,            w.w);
	row(w.x, w.y + w.h - 1,  w.w);

	if (w.h > 2) {
		SDL_FRect l = { (float)w.x,            (float)(w.y + 1), 1.0f, (float)(w.h - 2) };
		SDL_FRect r2 = { (float)(w.x + w.w - 1), (float)(w.y + 1), 1.0f, (float)(w.h - 2) };
		SDL_RenderFillRect(vng_ren, &l);
		SDL_RenderFillRect(vng_ren, &r2);
	}
}

void prim_box (SDL_FRect in, Uint32 inner, Uint32 outer)
{
	SDL_FRect out = { in.x - 1.0f, in.y - 1.0f, in.w + 2.0f, in.h + 2.0f };

	prim_rect(out, outer);
	prim_rect(in,  inner);
}
