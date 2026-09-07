#include "resize.h"
#include "view.h"

#define GRIP      5.0f    /* the drawn square, in screen pixels */
#define GRAB      5.0f    /* how far outside the square the mouse is still caught. The
                           * grip is deliberately tiny so it does not sit on the artwork,
                           * and a 5 pixel target is a test of aim - so the area that
                           * answers is bigger than the area that is drawn. */

/* Which corner. The two bits are the two axes, so the maths below never needs a switch:
 * bit 0 is "the x that moves is the right edge", bit 1 the same for the bottom. */
enum { C_TL = 0, C_TR = 1, C_BL = 2, C_BR = 3, C_NONE = -1 };

static int  held = C_NONE;

/* The canvas being drawn, in DOCUMENT coordinates of the tab as it is right now. Kept
 * as a rectangle rather than a width and height because dragging a top or left corner
 * moves the origin, and the origin is what tells vng_tab_resize where the old image
 * goes inside the new buffer. */
static float px0, py0, px1, py1;

static SDL_FPoint corner_screen (VNG_TAB *t, int c)
{
	float wx = (c & 1) ? (float)t->w : 0.0f;
	float wy = (c & 2) ? (float)t->h : 0.0f;
	return view_world_to_screen(t, wx, wy);
}

/*
 * The grip sits OUTSIDE the sheet, diagonally out from its corner, meeting the canvas at
 * exactly one point and covering none of it.
 *
 * That placement is the whole reason it can be this small. A grip centred on the corner
 * hides the four pixels a person is most likely to be looking at while deciding where
 * the edge should go - and on a 16x16 sprite at 1:1, four pixels is a quarter of the
 * width. Pushed outside, it never lands on the artwork at any zoom.
 *
 * The two bits of c do the placement with no branching: bit 0 says the corner is on the
 * right edge, so the grip extends right; bit 1 says the same for the bottom.
 */
static SDL_FRect grip_rect (VNG_TAB *t, int c)
{
	SDL_FPoint p = corner_screen(t, c);
	SDL_FRect  r = { SDL_floorf((c & 1) ? p.x : p.x - GRIP),
	                 SDL_floorf((c & 2) ? p.y : p.y - GRIP),
	                 GRIP, GRIP };
	return r;
}

static int corner_at (VNG_TAB *t, float sx, float sy)
{
	for (int c = 0; c < 4; c++) {
		SDL_FRect r = grip_rect(t, c);
		if (sx >= r.x - GRAB && sx <= r.x + r.w + GRAB &&
		    sy >= r.y - GRAB && sy <= r.y + r.h + GRAB)
			return c;
	}
	return C_NONE;
}

/* Rounds the pending rectangle into whole pixels and hands back what vng_tab_resize
 * wants: the new size, and where the old origin lands inside it. */
static void pending (int *w, int *h, int *dx, int *dy)
{
	int x0 = (int)SDL_roundf(px0), y0 = (int)SDL_roundf(py0);
	int x1 = (int)SDL_roundf(px1), y1 = (int)SDL_roundf(py1);

	*w = x1 - x0; if (*w < 1) *w = 1;
	*h = y1 - y0; if (*h < 1) *h = 1;
	*dx = -x0;
	*dy = -y0;
}

bool resize_event (const SDL_Event *e, VNG_TAB *t)
{
	if (!t) return false;

	switch (e->type) {

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		if (e->button.button != SDL_BUTTON_LEFT) return false;

		/* Space plus left is the pan gesture, and it wins over a grip: a hand that has
		 * already said "I am panning" must not resize the canvas by landing on one. */
		const bool *keys = SDL_GetKeyboardState(NULL);
		if (keys && keys[SDL_SCANCODE_SPACE]) return false;

		int c = corner_at(t, e->button.x, e->button.y);
		if (c == C_NONE) return false;

		held = c;
		px0 = 0.0f;        py0 = 0.0f;
		px1 = (float)t->w; py1 = (float)t->h;
		return true;
	}

	case SDL_EVENT_MOUSE_MOTION: {
		if (held == C_NONE) return false;

		SDL_FPoint w = view_screen_to_world(t, e->motion.x, e->motion.y);

		/* Only the held corner follows the hand; the other one is the anchor. */
		if (held & 1) px1 = w.x; else px0 = w.x;
		if (held & 2) py1 = w.y; else py0 = w.y;
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_UP: {
		if (held == C_NONE) return false;
		if (e->button.button != SDL_BUTTON_LEFT) return false;

		int w, h, dx, dy;
		pending(&w, &h, &dx, &dy);
		held = C_NONE;

		if (w == t->w && h == t->h && dx == 0 && dy == 0)
			return true;            /* a click, or a drag that came back */

		if (vng_tab_resize(t, w, h, dx, dy)) {
			/* The image just moved inside its own coordinates - a canvas that grew to
			 * the left put the old pixel 0 at pixel dx. Shifting the camera by the same
			 * amount keeps the drawing under the eye instead of letting it jump. */
			t->off_x += dx;
			t->off_y += dy;
		}
		return true;
	}

	default:
		return false;
	}
}

void resize_draw (VNG_TAB *t)
{
	if (!t) return;

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	if (held != C_NONE) {
		SDL_FPoint a = view_world_to_screen(t, px0, py0);
		SDL_FPoint b = view_world_to_screen(t, px1, py1);

		SDL_FRect box = { SDL_floorf(SDL_min(a.x, b.x)), SDL_floorf(SDL_min(a.y, b.y)),
		                  SDL_floorf(SDL_fabsf(b.x - a.x)), SDL_floorf(SDL_fabsf(b.y - a.y)) };

		/* An outline, not a fill: the drawing underneath is what the person is judging
		 * the new size against, and a wash over it defeats the purpose. */
		SDL_SetRenderDrawColor(vng_ren, 0x4C, 0x9A, 0xFF, 0xFF);
		SDL_RenderRect(vng_ren, &box);

		int w, h, dx, dy;
		pending(&w, &h, &dx, &dy);
		text_print(vng_text, box.x, box.y + box.h + 4.0f, 0x4C9AFFFF, "%d x %d", w, h);
	}

	for (int c = 0; c < 4; c++) {
		SDL_FRect r = grip_rect(t, c);

		/* The held grip goes white so the hand can see which corner it actually caught
		 * when two of them are close together on a small canvas. */
		if (c == held) SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xFF);
		else           SDL_SetRenderDrawColor(vng_ren, 0x4C, 0x9A, 0xFF, 0xFF);
		SDL_RenderFillRect(vng_ren, &r);
	}
}
