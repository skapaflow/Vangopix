#include "view.h"
#include "keys.h"

/* The desk around the paper when the sheet is framed: without it the white touches the
 * window edge and the eye loses where the document ends. It applies to view_reset only
 * - once a hand has panned, the sheet goes wherever it was put. */
#define MARGIN 24.0f

/*
 * The steps. Every value at or above 1 is an integer, which is the point: at 3x one art
 * pixel is a 3x3 square, at 3.5x it is three pixels wide on some columns and four on
 * others. Below 1 the fractions are for looking at a photograph that does not fit, and
 * there the renderer switches to linear filtering anyway.
 */
static const float STEPS[] = {
	0.0625f, 0.125f, 0.25f, 0.5f,
	1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 24.0f, 32.0f, 48.0f, 64.0f
};
#define STEP_LOT ((int)(sizeof STEPS / sizeof STEPS[0]))

/* Panning state. It is global rather than per tab because a hand can only drag one
 * document at a time, and the drag dies with the mouse button, not with the tab. */
static bool  panning   = false;
static float pan_wx    = 0.0f;   /* the world point the hand took hold of */
static float pan_wy    = 0.0f;

/*
 * THE SHEET'S ORIGIN IS ON A WHOLE SCREEN PIXEL, AND THE CAMERA'S OWN STATE IS NOT TOUCHED
 * TO GET IT THERE. Those are two separate sentences and keeping them separate is the whole
 * of this.
 *
 * THE BUG. view_sheet_rect floored the sheet onto whole pixels - correctly, and for the
 * reason written there - and NOTHING ELSE KNEW. Every annotation about the sheet came off the
 * unfloored transform: the outline round the pixel under the pointer, the selection's
 * rectangle, the corner grips, the 1:1 panel's marker. So did the hit test that decides which
 * pixel a press lands on. Measured at 1:1 with an offset of -10.3: the sheet is drawn at x=10,
 * so document pixel 0 covers screen [10,11), while the transform reported it at 10.3 - and
 * THIRTY PERCENT of the clicks inside a drawn pixel resolved to its neighbour. You point at a
 * pixel and the one beside it changes, after any pan, which is to say always. It is the same
 * shape of mistake as the colour disc whose rim was computed by different maths than its fill.
 *
 * WHAT DOES NOT WORK, and it was tried first: quantising off_x itself. It puts the origin on
 * a whole pixel and it wrecks the zoom, because the camera then stores a value that has been
 * rounded. A rounding of at most half a screen pixel at 4x is an eighth of a document pixel,
 * and zooming to 64x MAGNIFIES that into eight screen pixels; twelve notches measured fifteen
 * pixels of drift. The offset has to stay exact.
 *
 * SO THE SNAP LIVES AT THE BOUNDARY AND IS NEVER WRITTEN BACK. off_x and off_y remain the
 * continuous, exact values the zoom and the pan compute; the floor is applied when the world
 * is turned into screen coordinates, and the inverse undoes exactly that same floor. The pair
 * below are therefore exact inverses of each other AND of where the sheet is drawn - which is
 * the property that was missing - while the camera's arithmetic is untouched.
 */
static float origin_x (VNG_TAB *t) { return SDL_floorf(-t->off_x * t->zoom); }
static float origin_y (VNG_TAB *t) { return SDL_floorf(-t->off_y * t->zoom); }

SDL_FPoint view_world_to_screen (VNG_TAB *t, float wx, float wy)
{
	SDL_FPoint p = { origin_x(t) + wx * t->zoom, origin_y(t) + wy * t->zoom };
	return p;
}

SDL_FPoint view_screen_to_world (VNG_TAB *t, float sx, float sy)
{
	SDL_FPoint p = { (sx - origin_x(t)) / t->zoom, (sy - origin_y(t)) / t->zoom };
	return p;
}

/*
 * The camera's own view of the same question, WITHOUT the snap - and it is private on
 * purpose. The zoom and the pan feed their answers straight back into off_x and off_y, so
 * they are the two callers that must not see a rounded number: a floor read here would be a
 * floor stored there, which is the failure described above.
 *
 * Everything that draws or hit-tests uses the pair above. Everything that MOVES THE CAMERA
 * uses this.
 */
static SDL_FPoint raw_to_world (VNG_TAB *t, float sx, float sy)
{
	SDL_FPoint p = { sx / t->zoom + t->off_x, sy / t->zoom + t->off_y };
	return p;
}

/* Puts the world point (wx, wy) at the centre of the window. */
static void view_center_on (VNG_TAB *t, float wx, float wy)
{
	t->off_x = wx - (vng_win_w * 0.5f) / t->zoom;
	t->off_y = wy - (vng_win_h * 0.5f) / t->zoom;
}

void view_reset (VNG_TAB *t)
{
	if (!t) return;

	float aw = vng_win_w - 2.0f * MARGIN;
	float ah = vng_win_h - 2.0f * MARGIN;
	if (aw < 1.0f) aw = 1.0f;
	if (ah < 1.0f) ah = 1.0f;

	/* Same two rules the first version had, and for the same reason. When the sheet
	 * fits, the zoom is floored to an integer so one art pixel is an exact square. When
	 * it does not fit - a 4000px photograph - an integer zoom would be 0, and there the
	 * fractional reduction is right: seeing the whole image beats a perfect grid. */
	float fit = SDL_min(aw / t->w, ah / t->h);
	t->zoom = fit >= 1.0f ? SDL_floorf(fit) : fit;

	/* Clamped to the top of the ladder, and this is not cosmetic. A 16x16 sprite fits an
	 * 800x600 window at 34x, which is ABOVE every step - so nearest_step lands on the
	 * last one, 32, and the first notch UP would zoom OUT. The wheel turning the wrong
	 * way is the kind of defect a person blames on their mouse. */
	if (t->zoom > STEPS[STEP_LOT - 1])
		t->zoom = STEPS[STEP_LOT - 1];

	view_center_on(t, t->w * 0.5f, t->h * 0.5f);
}

void view_actual_size (VNG_TAB *t)
{
	if (!t) return;

	/* Keeps whatever is in the middle of the window in the middle of the window. Going
	 * to 1:1 by resetting the offset instead would throw the eye back to the centre of
	 * the document, which is rarely where the work is. */
	SDL_FPoint c = raw_to_world(t, vng_win_w * 0.5f, vng_win_h * 0.5f);
	t->zoom = 1.0f;
	view_center_on(t, c.x, c.y);
}

/* The step nearest to the current zoom.
 *
 * It has to be a search rather than a stored index because view_reset produces a zoom
 * that is NOT on the table - the fit of an odd-sized image is whatever it is. Snapping
 * on the first wheel notch is what lets an arbitrary framing rejoin the ladder. */
static int nearest_step (float zoom)
{
	int   best = 0;
	float bestd = SDL_fabsf(STEPS[0] - zoom);

	for (int i = 1; i < STEP_LOT; i++) {
		float d = SDL_fabsf(STEPS[i] - zoom);
		if (d < bestd) { bestd = d; best = i; }
	}
	return best;
}

/*
 * One notch, one step.
 *
 * Skyonara damps the wheel here: its index advances 0.7 per notch and the step comes
 * from (int)index, so a spurious notch does not always change the scale. That defends
 * against a wheel that fires on its own, but the cost is that the ladder is uneven -
 * from index 2.0 the scale changes after 2, 2, 1, 1, 2, 1, 1 notches. In a game the eye
 * never counts; in an editor, a zoom that answers some notches and swallows others
 * reads as a broken mouse. Predictable wins here.
 */
static void view_zoom_by (VNG_TAB *t, int notches, float at_x, float at_y)
{
	if (!notches) return;

	/* 1. The world point under the cursor, BEFORE the scale moves. */
	SDL_FPoint before = raw_to_world(t, at_x, at_y);

	/* 2. The step. No interpolation on the way: during a lerp the scale passes through
	 *    fractional values, and avoiding exactly those is why the steps exist. */
	int i = nearest_step(t->zoom) + notches;
	if (i < 0)              i = 0;
	if (i > STEP_LOT - 1)   i = STEP_LOT - 1;
	t->zoom = STEPS[i];

	/* 3. Put that world point back under the same pixel. This is what makes the zoom
	 *    happen at the cursor instead of at the corner of the window. */
	SDL_FPoint after = raw_to_world(t, at_x, at_y);
	t->off_x += before.x - after.x;
	t->off_y += before.y - after.y;
}

static bool pan_button (const SDL_Event *e)
{
	if (e->button.button == SDL_BUTTON_MIDDLE) return true;

	/* Space plus left drag, the gesture every image editor has. It is worth carrying
	 * even though the middle button already pans: a trackpad has no middle button, and
	 * this program is meant to build on three platforms. */
	if (e->button.button == SDL_BUTTON_LEFT) {
		/* Through keys_held and not SDL_GetKeyboardState: while a text field owns the
		 * keyboard, a space typed into it is a space, not a pan. */
		return keys_held(SDL_SCANCODE_SPACE);
	}
	return false;
}

bool view_event (const SDL_Event *e, VNG_TAB *t)
{
	if (!t) return false;

	switch (e->type) {

	case SDL_EVENT_MOUSE_WHEEL: {
		/* integer_y, not y. A mouse wheel delivers whole notches, but a trackpad
		 * delivers fractions - and (int)0.4f is zero, so casting y would make the zoom
		 * simply not respond on a macOS trackpad. SDL accumulates the fractions into
		 * whole ticks for us. */
		/* SHIFT+wheel is the TOOL'S, not the camera's: it sizes the tip. Handing it over
		 * rather than claiming it is what lets the tool sit at the end of the chain and
		 * still answer a gesture that arrives before it. */
		if (keys_mods() & SDL_KMOD_SHIFT) return false;

		int notches = e->wheel.integer_y;
		if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
			notches = -notches;

		/* The position carried BY the event, not the mouse's position now: by the time
		 * a queued event is handled the pointer has usually moved on, and the zoom
		 * would centre on the wrong pixel. */
		view_zoom_by(t, notches, e->wheel.mouse_x, e->wheel.mouse_y);
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		if (!pan_button(e)) return false;

		SDL_FPoint w = raw_to_world(t, e->button.x, e->button.y);
		pan_wx  = w.x;
		pan_wy  = w.y;
		panning = true;
		return true;
	}

	case SDL_EVENT_MOUSE_MOTION: {
		if (!panning) return false;

		/* offset = grabbed world point - cursor in world units. Assignment, not
		 * accumulation: the offset is derived from where the hand started and where it
		 * is now, so no drift can build up over a long drag. */
		t->off_x = pan_wx - e->motion.x / t->zoom;
		t->off_y = pan_wy - e->motion.y / t->zoom;
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_UP: {
		if (!panning) return false;
		panning = false;
		return true;
	}

	default:
		return false;
	}
}

SDL_FRect view_sheet_rect (VNG_TAB *t)
{
	/* A tab is born with zoom 0, which means "never framed". Framing happens here, on
	 * the first draw, rather than at creation: only here is the window size certainly
	 * the size the sheet has to fit into. It also means a resize does NOT reframe - a
	 * window that grows must not throw away where the hand put the drawing. */
	if (t->zoom <= 0.0f)
		view_reset(t);

	SDL_FPoint o = view_world_to_screen(t, 0.0f, 0.0f);

	/* Landed on whole pixels. A sheet at 3x starting on a half pixel makes the filter round
	 * differently along the edge, and the outermost column comes out a different width from
	 * the rest - the flaw that gives away a badly made editor.
	 *
	 * The origin arrives whole already - view_world_to_screen floors it - so these two floors
	 * are the statement of the requirement rather than the thing that meets it. That
	 * distinction IS the bug this used to have: when the flooring lived only here, every
	 * annotation and the hit test came off the unfloored transform and missed the sheet by
	 * exactly the fraction thrown away.
	 *
	 * The two on the SIZE still do work. Below 1:1 the sheet's width is w * zoom and
	 * fractional by definition; only the origin can be whole there, which is fine, because at
	 * that scale the renderer is filtering anyway and the image is being looked at rather
	 * than drawn on. */
	SDL_FRect r = { SDL_floorf(o.x), SDL_floorf(o.y),
	                SDL_floorf(t->w * t->zoom), SDL_floorf(t->h * t->zoom) };
	return r;
}
