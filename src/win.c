#include "win.h"

#define BORDER   1.0f
#define CLOSE_W 14.0f
#define GRIP    12.0f    /* the stretch corner, bottom right */
#define PAD      5.0f

/* Enough of the head must stay on screen to take hold of again. A window dragged off the
 * bottom of a window that is then made smaller is otherwise gone for good. */
#define KEEP    24.0f

struct _vng_win_ {
	char      title[48];
	SDL_FRect a;            /* the INTERIOR, in screen pixels */
	SDL_FPoint min;
	WIN_DRAW  draw;
	WIN_EVENT ev;
	void     *ctx;
	bool      shown;

	struct _vng_win_ *next; /* front of the list is the BACK of the z-order */
};

static VNG_WIN *list = NULL;

/* What is being dragged or stretched right now, and where it was taken hold of. */
static VNG_WIN *held = NULL;
static bool     stretching = false;
static float    grab_x = 0.0f, grab_y = 0.0f;

/* The window whose OWNER took a press. Everything that follows until the release goes to it,
 * even off the window: a slider dragged past its own edge is still being dragged. */
static VNG_WIN *inner = NULL;

/* The close box a press went down on. The same contract as every other button here: it only
 * fires if the button comes back up over the same box. */
static VNG_WIN *close_armed = NULL;

static float head_h (void)
{
	float h = vng_text ? text_line_height(vng_text) : 0.0f;
	return h > 1.0f ? h + 4.0f : 18.0f;
}

/* The whole window, frame included. The interior is what everything else is measured from,
 * because the interior is the part that means anything. */
static SDL_FRect outer (VNG_WIN *w)
{
	SDL_FRect r = { w->a.x - BORDER,
	                w->a.y - head_h(),
	                w->a.w + BORDER * 2.0f,
	                w->a.h + head_h() + BORDER };
	return r;
}

static SDL_FRect head_rect (VNG_WIN *w)
{
	SDL_FRect o = outer(w);
	SDL_FRect r = { o.x, o.y, o.w, head_h() };
	return r;
}

static SDL_FRect close_rect (VNG_WIN *w)
{
	SDL_FRect h = head_rect(w);
	SDL_FRect r = { h.x + h.w - CLOSE_W - PAD * 0.5f, h.y, CLOSE_W, h.h };
	return r;
}

static SDL_FRect grip_rect (VNG_WIN *w)
{
	SDL_FRect o = outer(w);
	SDL_FRect r = { o.x + o.w - GRIP, o.y + o.h - GRIP, GRIP, GRIP };
	return r;
}

static bool in_rect (SDL_FRect r, float x, float y)
{
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

VNG_WIN *win_top (void)
{
	VNG_WIN *found = NULL;
	for (VNG_WIN *w = list; w; w = w->next)
		if (w->shown) found = w;
	return found;
}

SDL_FRect win_area (VNG_WIN *w)
{
	SDL_FRect none = { 0.0f, 0.0f, 0.0f, 0.0f };
	return w ? w->a : none;
}

bool win_visible (VNG_WIN *w) { return w && w->shown; }

bool win_hover (VNG_WIN *w)
{
	if (!w || !w->shown) return false;

	float mx, my;
	SDL_GetMouseState(&mx, &my);
	return in_rect(outer(w), mx, my);
}

/* Declared here because showing a window raises it: a window being summoned is the one being
 * asked for, so it cannot come up behind another. */
static void raise (VNG_WIN *w);

void win_show (VNG_WIN *w, bool on)
{
	if (!w) return;
	w->shown = on;
	if (on) raise(w);

	/* Hiding whatever was being carried, rather than leaving a drag pointed at something
	 * nobody can see. */
	if (!on && held == w) { held = NULL; stretching = false; }
	if (!on && inner == w) inner = NULL;
	if (!on && close_armed == w) close_armed = NULL;
}

VNG_WIN *win_open (const char *title, SDL_FRect area, SDL_FPoint min,
                   WIN_DRAW draw, WIN_EVENT ev, void *ctx)
{
	VNG_WIN *w = (VNG_WIN *) SDL_calloc(1, sizeof *w);
	if (!w) return NULL;

	SDL_strlcpy(w->title, title ? title : "", sizeof w->title);
	w->a     = area;
	w->min   = min;
	w->draw  = draw;
	w->ev    = ev;
	w->ctx   = ctx;
	w->shown = true;

	/* On the end of the list, which is the front of the z-order: a window that has just been
	 * asked for is the one being looked at. */
	VNG_WIN **p = &list;
	while (*p) p = &(*p)->next;
	*p = w;
	return w;
}

void win_free (void)
{
	for (VNG_WIN *w = list; w; ) {
		VNG_WIN *n = w->next;
		SDL_free(w);
		w = n;
	}
	list = NULL;
	held = inner = close_armed = NULL;
}

/* To the end of the list, which draws last and is therefore on top. A relink and not a sort:
 * the list IS the order, the same decision the tab bar made. */
static void raise (VNG_WIN *w)
{
	if (!list || !w || !w->next) return;   /* already at the front */

	VNG_WIN **p = &list;
	while (*p && *p != w) p = &(*p)->next;
	if (!*p) return;

	*p = w->next;
	w->next = NULL;

	VNG_WIN **e = &list;
	while (*e) e = &(*e)->next;
	*e = w;
}

/* Topmost first, which is the reverse of the list. */
static VNG_WIN *at (float x, float y)
{
	VNG_WIN *found = NULL;
	for (VNG_WIN *w = list; w; w = w->next)
		if (w->shown && in_rect(outer(w), x, y))
			found = w;
	return found;
}

/* Keeps enough of the head reachable. Not the whole window: dragging most of a panel off the
 * edge is a thing people do on purpose, and only being unable to get it back is the problem. */
static void keep_reachable (VNG_WIN *w)
{
	float hh = head_h();

	if (w->a.x + w->a.w < KEEP)          w->a.x = KEEP - w->a.w;
	if (w->a.x > vng_win_w - KEEP)       w->a.x = vng_win_w - KEEP;
	if (w->a.y - hh < 0.0f)              w->a.y = hh;
	if (w->a.y - hh > vng_win_h - KEEP)  w->a.y = vng_win_h - KEEP + hh;
}

void win_place (VNG_WIN *w, float cx, float cy)
{
	if (!w) return;

	/* Centred on the WHOLE window, head bar included - which is what "the middle of it lands
	 * under the pointer" means to the eye, and what the original measured. */
	SDL_FRect o = outer(w);

	w->a.x += cx - (o.x + o.w * 0.5f);
	w->a.y += cy - (o.y + o.h * 0.5f);

	keep_reachable(w);
}

bool win_event (const SDL_Event *e)
{
	switch (e->type) {

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		float x = e->button.x, y = e->button.y;

		VNG_WIN *w = at(x, y);
		if (!w) return false;

		raise(w);

		/* The frame answers the LEFT button only: dragging a window about with the right one
		 * is not a gesture anybody makes, and reserving it here would take it away from the
		 * inside - where it means something, since the two colours are one per button. */
		bool on_frame = in_rect(head_rect(w), x, y) || in_rect(grip_rect(w), x, y);

		if (e->button.button == SDL_BUTTON_LEFT) {
			if (in_rect(close_rect(w), x, y)) { close_armed = w; return true; }

			if (in_rect(grip_rect(w), x, y)) {
				held = w;
				stretching = true;
				grab_x = x - (w->a.x + w->a.w);
				grab_y = y - (w->a.y + w->a.h);
				return true;
			}

			if (in_rect(head_rect(w), x, y)) {
				held = w;
				stretching = false;
				grab_x = x - w->a.x;
				grab_y = y - w->a.y;
				return true;
			}
		}

		/* The interior, EITHER BUTTON. Whatever the owner says, the event stops here - it
		 * landed on a window, and the sheet underneath must not see it. */
		if (!on_frame && w->ev && w->ev(w->a, e, w->ctx))
			inner = w;
		return true;
	}

	case SDL_EVENT_MOUSE_MOTION: {
		/* Off the window is still the owner's while its button is down. */
		if (inner) return inner->ev(inner->a, e, inner->ctx), true;

		if (!held) return false;

		float x = e->motion.x, y = e->motion.y;

		if (stretching) {
			held->a.w = x - grab_x - held->a.x;
			held->a.h = y - grab_y - held->a.y;
			if (held->a.w < held->min.x) held->a.w = held->min.x;
			if (held->a.h < held->min.y) held->a.h = held->min.y;
		} else {
			held->a.x = x - grab_x;
			held->a.y = y - grab_y;
			keep_reachable(held);
		}
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_UP: {
		if (inner) {
			VNG_WIN *w = inner;
			inner = NULL;
			w->ev(w->a, e, w->ctx);
			return true;
		}
		if (close_armed) {
			VNG_WIN *w = close_armed;
			close_armed = NULL;
			if (in_rect(close_rect(w), e->button.x, e->button.y)) {
				win_show(w, false);
				return true;
			}
		}
		if (!held) return false;

		held = NULL;
		stretching = false;
		return true;
	}

	case SDL_EVENT_MOUSE_WHEEL:
		/* Claimed over a window so the camera behind it does not zoom while somebody is
		 * pointing at something else. Windows that want the wheel will be given it here. */
		return at(e->wheel.mouse_x, e->wheel.mouse_y) != NULL;

	default:
		return false;
	}
}

void win_draw (void)
{
	float mx, my;
	SDL_GetMouseState(&mx, &my);

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	for (VNG_WIN *w = list; w; w = w->next) {
		if (!w->shown) continue;

		SDL_FRect o = outer(w);
		SDL_FRect h = head_rect(w);

		SDL_SetRenderDrawColor(vng_ren, 0x14, 0x14, 0x14, 0xF0);
		SDL_RenderFillRect(vng_ren, &o);

		SDL_SetRenderDrawColor(vng_ren, 0x22, 0x22, 0x22, 0xFF);
		SDL_RenderFillRect(vng_ren, &h);

		SDL_SetRenderDrawColor(vng_ren, 0x30, 0x30, 0x30, 0xFF);
		SDL_RenderRect(vng_ren, &o);

		if (vng_text) {
			/* Cut with a tilde like every other name in this program - text_fit is where
			 * that rule lives so a title and a tab agree. */
			char name[48];
			text_fit(vng_text, name, sizeof name, w->title,
			         h.w - CLOSE_W - PAD * 3.0f);
			text_print(vng_text, h.x + PAD, h.y + 2.0f, 0xB4B4B4FF, "%s", name);

			SDL_FRect c = close_rect(w);
			text_print(vng_text, c.x + 4.0f, c.y + 2.0f,
			           in_rect(c, mx, my) ? 0xFF6060FF : 0x707070FF, "x");
		}

		/* The stretch corner: two short strokes, which is enough to say "pull here" without
		 * being a thing on screen. */
		SDL_FRect g = grip_rect(w);
		SDL_SetRenderDrawColor(vng_ren, 0x60, 0x60, 0x60, 0xFF);
		for (float i = 3.0f; i < GRIP; i += 4.0f)
			SDL_RenderLine(vng_ren, g.x + g.w - i, g.y + g.h - 2.0f,
			                        g.x + g.w - 2.0f, g.y + g.h - i);

		if (!w->draw) continue;

		/* CLIPPED FOR THE OWNER. It is the one service a frame can give that its owner would
		 * otherwise have to remember on every line it draws. */
		SDL_Rect clip = { (int)w->a.x, (int)w->a.y, (int)w->a.w, (int)w->a.h };
		SDL_SetRenderClipRect(vng_ren, &clip);
		w->draw(w->a, w->ctx);
		SDL_SetRenderClipRect(vng_ren, NULL);
	}
}
