#include "select.h"
#include "view.h"
#include "keys.h"
#include "tool.h"
#include "undo.h"
#include "core.h"

/* The dashes of the marching rectangle, in SCREEN pixels: it is an annotation about the
 * document, not part of it, so it stays the same size at any zoom. */
#define DASH  4.0f

/* How fast the dashes walk, in dashes per second. Slow enough to read as motion rather than
 * as flicker, which is the only thing this has to get right. */
#define MARCH 6.0f

struct _vng_sel_ {
	bool    on;             /* a rectangle is marked at all */
	int     x, y, w, h;     /* where it is NOW, in document pixels */
	int     sx, sy;         /* where the float was lifted from */
	bool    cut;            /* whether that place is to be left empty */

	Uint32 *pixels;         /* NULL while merely marked; the lifted pixels while floating */
	SDL_Texture *tex;
	bool    tex_stale;

	/* The drag in progress, if any. */
	bool    marking;        /* dragging out a new rectangle */
	bool    moving;         /* carrying the float */
	int     ax, ay;         /* where the drag began, document pixels */
	int     grab_x, grab_y; /* where inside the float it was taken hold of */
};

/* One buffer for the whole program, which is the whole of the cross-tab story. */
static Uint32 *clip = NULL;
static int     clip_w = 0, clip_h = 0;

static float march = 0.0f;

/* ------------------------------------------------------------------------ the state */

static VNG_SEL *sel_of (VNG_TAB *t)
{
	if (!t) return NULL;
	if (!t->sel) t->sel = (VNG_SEL *) SDL_calloc(1, sizeof *t->sel);
	return t->sel;
}

/* Frees the float and forgets it. The texture is destroyed and the pointer cleared IN ONE
 * PLACE, which is the answer to the first Vangopix's double free: every caller there
 * destroyed the texture itself and then called a helper that destroyed it again. */
static void float_drop (VNG_SEL *s)
{
	if (!s) return;

	if (s->tex) SDL_DestroyTexture(s->tex);
	SDL_free(s->pixels);

	s->tex    = NULL;
	s->pixels = NULL;
	s->cut    = false;
}

void select_free (VNG_SEL *s)
{
	if (!s) return;
	float_drop(s);
	SDL_free(s);
}

void select_clipboard_free (void)
{
	SDL_free(clip);
	clip = NULL;
	clip_w = clip_h = 0;
}

/* ------------------------------------------------------------------------ the pixels */

static void clamp_rect (VNG_TAB *t, int *x, int *y, int *w, int *h)
{
	if (*x < 0) { *w += *x; *x = 0; }
	if (*y < 0) { *h += *y; *y = 0; }
	if (*x + *w > t->w) *w = t->w - *x;
	if (*y + *h > t->h) *h = t->h - *y;
	if (*w < 0) *w = 0;
	if (*h < 0) *h = 0;
}

/* Takes a copy of a rectangle of the document. NULL if there is nothing there to take. */
static Uint32 *grab (VNG_TAB *t, int x, int y, int w, int h)
{
	if (w < 1 || h < 1) return NULL;

	Uint32 *out = (Uint32 *) SDL_calloc((size_t)w * h, sizeof(Uint32));
	if (!out) return NULL;

	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++) {
			int px = x + i, py = y + j;
			if (px < 0 || py < 0 || px >= t->w || py >= t->h) continue;
			out[j * w + i] = t->pixels[(size_t)py * t->w + px];
		}
	return out;
}

/*
 * Lifts the marked rectangle into a float.
 *
 * `cut` is what tells a MOVE from a COPY, and it is only a flag on the float: nothing is
 * written to the document here. The hole is drawn until the float is dropped, and that is
 * what leaves nothing to clean up if it never is.
 */
static bool lift (VNG_TAB *t, VNG_SEL *s, bool cut)
{
	if (!s->on || s->pixels) return s->pixels != NULL;

	s->pixels = grab(t, s->x, s->y, s->w, s->h);
	if (!s->pixels) return false;

	s->sx  = s->x;
	s->sy  = s->y;
	s->cut = cut;
	s->tex_stale = true;
	return true;
}

/*
 * Puts the float into the document as ONE undo step: the source rectangle emptied, then the
 * pixels written where they now are.
 *
 * The two rectangles may overlap, and that is the case worth naming - a selection nudged by
 * one pixel. Both writes happen, both carries are recorded, and undo walks them backwards,
 * so the overlap comes out right without anything having to reason about it.
 */
void select_commit (VNG_TAB *t)
{
	VNG_SEL *s = t ? t->sel : NULL;
	if (!s || !s->pixels) return;

	/* Direct, always: what a cut leaves may be transparent, and transparency cannot be shown
	 * by compositing a preview over the sheet - see tabs.h. Everything a selection does is
	 * one step from the person's side, so it may as well be one kind of stroke. */
	if (!vng_tab_stroke_open(t, true)) { float_drop(s); return; }

	/* WHAT A CUT LEAVES BEHIND IS COLOUR 2, not a hole and not white. The second colour is
	 * already "what the right button lays down" - the background of the moment - so cutting
	 * leaving it is the same idea said once more. It defaults to nothing, so the default
	 * behaviour is still a hole; load it with white and a cut leaves paper. */
	if (s->cut) {
		Uint32 back = tool_colour(1);
		for (int j = 0; j < s->h; j++)
			for (int i = 0; i < s->w; i++)
				vng_tab_put(t, s->sx + i, s->sy + j, back);
	}

	for (int j = 0; j < s->h; j++)
		for (int i = 0; i < s->w; i++) {
			Uint32 c = s->pixels[j * s->w + i];
			if ((c >> 24) != 0)   /* a transparent pixel of the float leaves what is under it */
				vng_tab_put(t, s->x + i, s->y + j, c);
		}

	vng_tab_stroke_close(t);
	float_drop(s);
}

/* Throws the float away and puts the rectangle back where it was lifted from. Nothing was
 * written, so there is nothing to undo - which is the point of not writing. */
static void cancel (VNG_SEL *s)
{
	if (!s->pixels) { s->on = false; return; }

	s->x = s->sx;
	s->y = s->sy;
	float_drop(s);
}

/* ------------------------------------------------------------------- the transforms */

static void flip_h (Uint32 *p, int w, int h)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w / 2; x++) {
			Uint32 tmp = p[y * w + x];
			p[y * w + x] = p[y * w + (w - 1 - x)];
			p[y * w + (w - 1 - x)] = tmp;
		}
}

static void flip_v (Uint32 *p, int w, int h)
{
	for (int y = 0; y < h / 2; y++)
		for (int x = 0; x < w; x++) {
			Uint32 tmp = p[y * w + x];
			p[y * w + x] = p[(h - 1 - y) * w + x];
			p[(h - 1 - y) * w + x] = tmp;
		}
}

/* The colour is inverted and the ALPHA IS NOT. Inverting alpha turns a drawing inside out -
 * everything that was there disappears and everything that was not appears - which is never
 * what anyone means by "invert". */
static void invert (Uint32 *p, int n)
{
	for (int i = 0; i < n; i++)
		p[i] = (p[i] & 0xFF000000u) | (~p[i] & 0x00FFFFFFu);
}

/* A quarter turn clockwise into a new buffer, because the result has the other shape. */
static bool rotate (VNG_SEL *s)
{
	int w = s->w, h = s->h;

	Uint32 *out = (Uint32 *) SDL_calloc((size_t)w * h, sizeof(Uint32));
	if (!out) return false;

	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			out[x * h + (h - 1 - y)] = s->pixels[y * w + x];

	SDL_free(s->pixels);
	s->pixels = out;

	/* Turned about its CENTRE, so a rotation does not walk the selection across the sheet. */
	s->x += (w - h) / 2;
	s->y += (h - w) / 2;
	s->w = h;
	s->h = w;
	return true;
}

/* ------------------------------------------------------------------------- the events */

/* Clamped to the sheet as it is dragged. Selecting what is not there is meaningless, and a
 * drag that ran a long way off the canvas at a low zoom would otherwise ask for a buffer of
 * thousands of pixels a side the moment it was lifted. */
static void mark_from_drag (VNG_TAB *t, VNG_SEL *s, int x, int y)
{
	int x0 = s->ax < x ? s->ax : x, x1 = s->ax < x ? x : s->ax;
	int y0 = s->ay < y ? s->ay : y, y1 = s->ay < y ? y : s->ay;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > t->w - 1) x1 = t->w - 1;
	if (y1 > t->h - 1) y1 = t->h - 1;

	s->x = x0;
	s->y = y0;
	s->w = x1 - x0 + 1;
	s->h = y1 - y0 + 1;
}

static bool inside_sel (VNG_SEL *s, int x, int y)
{
	return s->on && x >= s->x && y >= s->y && x < s->x + s->w && y < s->y + s->h;
}

static Uint32 *grab_from_float (VNG_SEL *s);

static void copy_out (VNG_TAB *t, VNG_SEL *s)
{
	if (!s->on) return;

	Uint32 *take = s->pixels ? grab_from_float(s) : grab(t, s->x, s->y, s->w, s->h);
	if (!take) return;

	SDL_free(clip);
	clip   = take;
	clip_w = s->w;
	clip_h = s->h;
}

void select_paste (VNG_TAB *t, int x, int y)
{
	VNG_SEL *s = sel_of(t);
	if (!s || !clip) return;

	select_commit(t);   /* whatever was floating lands before the new one arrives */

	s->pixels = (Uint32 *) SDL_calloc((size_t)clip_w * clip_h, sizeof(Uint32));
	if (!s->pixels) return;

	SDL_memcpy(s->pixels, clip, (size_t)clip_w * clip_h * sizeof(Uint32));

	s->w = clip_w;
	s->h = clip_h;
	s->x = s->sx = x - clip_w / 2;
	s->y = s->sy = y - clip_h / 2;
	s->on  = true;
	s->cut = false;      /* a paste has no source in this document to empty */
	s->tex_stale = true;

	tool_set(T_SELECT);
}

static void clear_marked (VNG_TAB *t, VNG_SEL *s)
{
	if (!s->on || s->pixels) return;

	int x = s->x, y = s->y, w = s->w, h = s->h;
	clamp_rect(t, &x, &y, &w, &h);
	if (w < 1 || h < 1) return;

	/* Direct, like everything else here: colour 2 may be nothing, and nothing cannot be shown
	 * by compositing a preview over the sheet. */
	if (!vng_tab_stroke_open(t, true)) return;

	Uint32 back = tool_colour(1);
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			vng_tab_put(t, x + i, y + j, back);

	vng_tab_stroke_close(t);
}

bool select_event (const SDL_Event *e, VNG_TAB *t)
{
	if (!t) return false;

	VNG_SEL *s = sel_of(t);
	if (!s) return false;

	bool tool_is_select = (tool_current() == T_SELECT);

	switch (e->type) {

	case SDL_EVENT_KEY_DOWN: {
		if (e->key.repeat) return false;

		SDL_Keymod m = e->key.mod;
		bool bare = (m & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0;

		if (m & SDL_KMOD_CTRL) {
			switch (e->key.key) {
			case SDLK_A:
				/* Everything, and the tool comes with it: select-all with no way to act on
				 * the selection would be a gesture that does nothing visible. */
				select_commit(t);
				s->on = true;
				s->x = s->y = 0;
				s->w = t->w;
				s->h = t->h;
				tool_set(T_SELECT);
				return true;

			case SDLK_C:
				if (!s->on) return false;
				copy_out(t, s);
				return true;

			case SDLK_X:
				if (!s->on) return false;
				copy_out(t, s);
				if (s->pixels) { s->cut = true; select_commit(t); }
				else           clear_marked(t, s);
				s->on = false;
				return true;

			case SDLK_V: {
				if (!clip) return false;
				float mx, my;
				SDL_GetMouseState(&mx, &my);
				SDL_FPoint w = view_screen_to_world(t, mx, my);
				select_paste(t, (int)SDL_floorf(w.x), (int)SDL_floorf(w.y));
				return true;
			}
			default: return false;
			}
		}

		if (!bare) return false;

		/* ESC gives up whatever is going on. It is claimed only when there IS something to
		 * give up, so it still raises the tab bar the rest of the time. */
		if (e->key.key == SDLK_ESCAPE && s->on) {
			cancel(s);
			s->on = s->marking = s->moving = false;
			return true;
		}

		if (e->key.key == SDLK_DELETE && s->on && tool_is_select) {
			if (s->pixels) { float_drop(s); }   /* a float deleted is simply not put down */
			else           clear_marked(t, s);
			s->on = false;
			return true;
		}

		/*
		 * THE TRANSFORMS ARE BARE KEYS, and this is the one place in the program where a
		 * bare key changes meaning. It is allowed because both halves of the condition are
		 * VISIBLE: the select tool is current and there is a rectangle on the sheet. R is
		 * still the ellipse everywhere else.
		 */
		if (!tool_is_select || !s->on) return false;

		if (e->key.key == SDLK_V || e->key.key == SDLK_H ||
		    e->key.key == SDLK_I || e->key.key == SDLK_R) {

			/* A transform acts on pixels, so a marked rectangle is lifted first - and lifted
			 * as a CUT, because a flipped selection has to leave its old place behind. */
			if (!lift(t, s, true)) return true;

			switch (e->key.key) {
			case SDLK_V: flip_v(s->pixels, s->w, s->h);      break;
			case SDLK_H: flip_h(s->pixels, s->w, s->h);      break;
			case SDLK_I: invert(s->pixels, s->w * s->h);     break;
			case SDLK_R: rotate(s);                          break;
			default: break;
			}
			s->tex_stale = true;
			return true;
		}
		return false;
	}

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		if (!tool_is_select || e->button.button != SDL_BUTTON_LEFT) return false;

		SDL_FPoint w = view_screen_to_world(t, e->button.x, e->button.y);
		int x = (int)SDL_floorf(w.x), y = (int)SDL_floorf(w.y);

		bool ctrl = (keys_mods() & SDL_KMOD_CTRL) != 0;

		/*
		 * A press INSIDE the rectangle takes hold of it, and CTRL makes that a COPY: the
		 * pixels come with the hand and the place they came from is left as it was.
		 *
		 * CTRL BELONGS TO THE SELECTION ONLY WHERE THE SELECTION IS. Anywhere else there is
		 * nothing to duplicate, so it means what it means everywhere else in the program and
		 * this file declines the event - which is what puts the eyedropper back. It had
		 * stopped working the moment the select tool was in hand, because this returned true
		 * for every left press it saw.
		 */
		if (inside_sel(s, x, y)) {
			if (!lift(t, s, !ctrl)) return true;
			s->moving = true;
			s->grab_x = x - s->x;
			s->grab_y = y - s->y;
			return true;
		}

		if (ctrl) return false;   /* the eyedropper's, and the tool is right behind us */

		select_commit(t);

		s->on = true;
		s->marking = true;
		s->ax = x;
		s->ay = y;
		mark_from_drag(t, s, x, y);
		return true;
	}

	case SDL_EVENT_MOUSE_MOTION: {
		if (!s->marking && !s->moving) return false;

		SDL_FPoint w = view_screen_to_world(t, e->motion.x, e->motion.y);
		int x = (int)SDL_floorf(w.x), y = (int)SDL_floorf(w.y);

		if (s->marking) mark_from_drag(t, s, x, y);
		else            { s->x = x - s->grab_x; s->y = y - s->grab_y; }
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_UP: {
		if (!s->marking && !s->moving) return false;
		if (e->button.button != SDL_BUTTON_LEFT) return false;

		/* A click rather than a drag marks nothing, and an empty rectangle on screen is a
		 * thing people then try to use. */
		if (s->marking && s->w <= 1 && s->h <= 1) s->on = false;

		s->marking = s->moving = false;
		return true;
	}

	default:
		return false;
	}
}

/* ------------------------------------------------------------------------- the pixels */

/* The float's own pixels, copied - what CTRL+C takes when there is something in the air
 * rather than something still in the document. */
static Uint32 *grab_from_float (VNG_SEL *s)
{
	size_t n = (size_t)s->w * s->h;
	Uint32 *out = (Uint32 *) SDL_malloc(n * sizeof(Uint32));
	if (out) SDL_memcpy(out, s->pixels, n * sizeof(Uint32));
	return out;
}

static void tex_make (VNG_SEL *s)
{
	if (!s->tex_stale || !s->pixels) return;

	/* Destroyed and rebuilt in ONE place, and the pointer is cleared before anything can
	 * look at it again. The first Vangopix destroyed it at the call site as well, which is
	 * what made every flip a double free. */
	if (s->tex) SDL_DestroyTexture(s->tex);
	s->tex = NULL;

	s->tex = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
	                           SDL_TEXTUREACCESS_STREAMING, s->w, s->h);
	if (!s->tex) return;

	SDL_SetTextureBlendMode(s->tex, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(s->tex, SDL_SCALEMODE_NEAREST);

	/* The pixels are uploaded AS THEY ARE. The first Vangopix rewrote them on the way out,
	 * turning everything matching the background colour transparent - destructively, on the
	 * buffer itself, every time - so an image flipped twice had eaten its own colour. */
	SDL_UpdateTexture(s->tex, NULL, s->pixels, s->w * (int)sizeof(Uint32));
	s->tex_stale = false;
}

/* The marching rectangle: black and white dashes that walk, so the outline reads against
 * any artwork and reads as a SELECTION rather than as something drawn. */
static void ants (SDL_FRect r) {

	float off = march;

	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xFF);
	SDL_RenderRect(vng_ren, &r);

	SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xFF);

	for (float x = r.x - off; x < r.x + r.w; x += DASH * 2.0f) {
		float x0 = x < r.x ? r.x : x;
		float x1 = x + DASH > r.x + r.w ? r.x + r.w : x + DASH;
		if (x1 <= x0) continue;
		SDL_RenderLine(vng_ren, x0, r.y, x1, r.y);
		SDL_RenderLine(vng_ren, x0, r.y + r.h - 1, x1, r.y + r.h - 1);
	}

	for (float y = r.y - off; y < r.y + r.h; y += DASH * 2.0f) {
		float y0 = y < r.y ? r.y : y;
		float y1 = y + DASH > r.y + r.h ? r.y + r.h : y + DASH;
		if (y1 <= y0) continue;
		SDL_RenderLine(vng_ren, r.x, y0, r.x, y1);
		SDL_RenderLine(vng_ren, r.x + r.w - 1, y0, r.x + r.w - 1, y1);
	}
}

static SDL_FRect on_screen (VNG_TAB *t, int x, int y, int w, int h)
{
	SDL_FPoint a = view_world_to_screen(t, (float)x, (float)y);
	SDL_FPoint b = view_world_to_screen(t, (float)(x + w), (float)(y + h));
	SDL_FRect  r = { a.x, a.y, b.x - a.x, b.y - a.y };
	return r;
}

void select_draw (VNG_TAB *t)
{
	VNG_SEL *s = t ? t->sel : NULL;
	if (!s || !s->on) return;

	/* Wrapped as it accumulates, not only where it is read: a float that grows for an hour
	 * loses the precision the dashes are measured in. */
	march = SDL_fmodf(march + vng_dt * MARCH * DASH, DASH * 2.0f);

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	/*
	 * The hole is DRAWN and not dug. Nothing has been written to the document, so a float
	 * that is never put down leaves no trace - and there is no half-finished sheet to repair
	 * if the program is closed with one in the air.
	 */
	if (s->pixels && s->cut) {
		SDL_FRect hole = on_screen(t, s->sx, s->sy, s->w, s->h);
		Uint32    back = tool_colour(1);

		/* THE HOLE SHOWS WHAT WILL ACTUALLY BE LEFT THERE, which is colour 2 - so the desk
		 * goes down first and the colour over it at its REAL alpha. A flat grey rectangle
		 * was a lie twice over: it looked like the desk when the colour was opaque, and it
		 * looked like a colour when the colour was nothing. */
		vangopix_desk_rect(hole);

		SDL_SetRenderDrawColor(vng_ren, (Uint8)((back >> 16) & 0xFF),
		                                (Uint8)((back >>  8) & 0xFF),
		                                (Uint8)( back        & 0xFF),
		                                (Uint8)((back >> 24) & 0xFF));
		SDL_RenderFillRect(vng_ren, &hole);
	}

	if (s->pixels) {
		tex_make(s);
		if (s->tex) {
			SDL_FRect dst = on_screen(t, s->x, s->y, s->w, s->h);
			SDL_RenderTexture(vng_ren, s->tex, NULL, &dst);
		}
	}

	ants(on_screen(t, s->x, s->y, s->w, s->h));
}
