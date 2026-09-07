#include "sidebar.h"
#include "project.h"
#include "tabbar.h"
#include "tabs.h"

#define BAR_W     260.0f
#define PAD         8.0f
#define INDENT     12.0f
#define CLOSE_W    14.0f
#define MAX_ROWS  4096   /* what a person can scroll through before giving up and using
                          * a file manager. A deep tree past this is truncated, not
                          * crashed - see rows_build. */

static bool  visible = false;
static float scroll  = 0.0f;

/*
 * How far in the panel is: 0 fully out to the left, 1 fully in. `visible` is only where
 * it is HEADED - every position on screen comes from `anim`, so a click during the slide
 * lands on the panel where the eye sees it and not where it will end up.
 *
 * The step is an exponential lerp rather than a fixed increment per frame, because the
 * loop is vsynced and a 144Hz monitor would otherwise open the panel twice as fast as a
 * 60Hz one. Feeding the elapsed time through 1 - e^(-RATE*dt) makes the curve a function
 * of seconds, so the panel takes the same time to arrive on any machine.
 *
 * A lerp only ever approaches its target, so the last half pixel is snapped: without it
 * `anim` never reaches 0, the panel keeps a sliver on screen for ever, and the sidebar
 * would go on eating clicks along the left edge after it was dismissed.
 */
#define ANIM_RATE  16.0f   /* 90% of the way in about 150ms, which is where the eye calls
                            * it arrived; below 10 the panel drags, above 25 the slide is
                            * over before it reads as motion at all */
#define ANIM_SNAP   0.002f /* half a pixel of 260 - past this nothing more is visible */

static float anim = 0.0f;

static void anim_step (void)
{
	float target = visible ? 1.0f : 0.0f;

	anim += (target - anim) * (1.0f - SDL_expf(-ANIM_RATE * vng_dt));
	if (SDL_fabsf(target - anim) < ANIM_SNAP) anim = target;
}

/* Where the left edge of the panel is right now: -BAR_W when out, 0 when in. Every
 * coordinate in this file, drawn or tested, is measured from it. */
static float slide (void) { return (anim - 1.0f) * BAR_W; }

/*
 * Where the top of the panel is: under the tab bar while the bar is up, at the top of
 * the window while it is down. Both float over the sheet and both claim the same corner,
 * so one of them has to give way, and the bar is the one that spans the whole width.
 *
 * It is NOT lerped, deliberately. The bar itself appears and disappears in a single
 * frame; easing the sidebar into place while the bar snaps would open a gap of
 * checkerboard between the two for the length of the ease. They move together because
 * they are one edge.
 */
static float top_y (void) { return tabbar_height(); }

typedef struct { VNG_NODE *n; int depth; } ROW;

static ROW rows[MAX_ROWS];
static int row_lot = 0;

/* The root whose [x] the button went down on. Same contract as the tab bar: the removal
 * only fires if the button comes back up over the same [x]. */
static VNG_NODE *close_armed = NULL;

void sidebar_toggle (void)
{
	visible = !visible;
	/* A press that was armed on an [x] is dropped along with the panel: the button will
	 * come up somewhere the sidebar is no longer listening, and an arming that outlives
	 * its panel fires on the next release, in the next session of it. */
	close_armed = NULL;
}

/* Reports where the panel is HEADED, not where it is. What asks - a folder dropped on a
 * hidden sidebar - wants to know whether to summon it, and a panel already on its way in
 * must not be toggled back out. */
bool sidebar_visible (void) { return visible; }

float sidebar_edge (void) { return anim > 0.0f ? slide() + BAR_W : 0.0f; }

static float row_h (void)
{
	float h = text_line_height(vng_text);
	return h > 1.0f ? h + 3.0f : 17.0f;
}

/*
 * Flattens the visible part of the tree into rows, in display order.
 *
 * Rebuilt on every event and every frame rather than cached, because it only walks what
 * is EXPANDED - a collapsed project costs one row no matter how much is under it. A
 * cache would have to be invalidated by every toggle, every add and every remove, and
 * the walk it saves is a few hundred pointer steps.
 */
static void rows_build (void)
{
	row_lot = 0;

	VNG_NODE *stack[64];
	int       depth[64];
	int       top = 0;

	/* Explicit stack rather than recursion: the depth of a directory tree is decided by
	 * whatever folder was dropped in, and a deep one must truncate rather than run the
	 * C stack out. */
	for (VNG_NODE *p = vng_projects; p; p = p->next) {
		if (top >= 64) break;
		stack[top] = p;
		depth[top] = 0;
		top++;
	}

	/* The roots went on in order, so they have to come off in order. */
	for (int i = 0; i < top / 2; i++) {
		VNG_NODE *tn = stack[i]; stack[i] = stack[top - 1 - i]; stack[top - 1 - i] = tn;
		int       td = depth[i]; depth[i] = depth[top - 1 - i]; depth[top - 1 - i] = td;
	}

	while (top > 0 && row_lot < MAX_ROWS) {
		VNG_NODE *n = stack[--top];
		int       d = depth[top];

		rows[row_lot].n     = n;
		rows[row_lot].depth = d;
		row_lot++;

		if (n->is_dir && n->open) {
			int first = top;
			for (VNG_NODE *c = n->child; c && top < 64; c = c->next) {
				stack[top] = c;
				depth[top] = d + 1;
				top++;
			}
			for (int i = first; i < first + (top - first) / 2; i++) {
				int j = top - 1 - (i - first);
				VNG_NODE *tn = stack[i]; stack[i] = stack[j]; stack[j] = tn;
				int       td = depth[i]; depth[i] = depth[j]; depth[j] = td;
			}
		}
	}
}

static float content_h (void) { return row_lot * row_h(); }

static void scroll_clamp (void)
{
	float over = content_h() - (vng_win_h - top_y() - PAD * 2.0f);
	if (over < 0.0f) over = 0.0f;
	if (scroll > over)  scroll = over;
	if (scroll < 0.0f)  scroll = 0.0f;
}

static int row_at (float x, float y)
{
	if (x < 0.0f || x >= BAR_W) return -1;

	float local = y - top_y() - PAD + scroll;
	if (local < 0.0f) return -1;

	int i = (int)(local / row_h());
	return (i >= 0 && i < row_lot) ? i : -1;
}

static float row_y (int i) { return top_y() + PAD + i * row_h() - scroll; }

bool sidebar_event (const SDL_Event *e)
{
	/* Not `visible`: while the panel is sliding out it still covers pixels, and a click
	 * on what a person can plainly see must not fall through to the sheet. At anim 0 the
	 * panel is off screen entirely and every test below misses on its own. */
	if (anim <= 0.0f) return false;

	float ox = slide();

	rows_build();

	switch (e->type) {

	case SDL_EVENT_MOUSE_WHEEL: {
		/* Only when the pointer is over the panel. Everywhere else the wheel is the
		 * camera's, and stealing it would make zooming stop working near the left edge
		 * of the window for no reason a person could see. */
		if (e->wheel.mouse_x - ox >= BAR_W) return false;

		scroll -= e->wheel.integer_y * row_h() * 3.0f;
		scroll_clamp();
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		float x = e->button.x - ox, y = e->button.y;
		if (x >= BAR_W) return false;
		if (e->button.button != SDL_BUTTON_LEFT) return true;

		int i = row_at(x, y);
		if (i < 0) return true;      /* empty panel space is still the panel's */

		VNG_NODE *n = rows[i].n;

		/* The [x] only exists on roots: a subfolder is not something the person added,
		 * so there is nothing to take away. */
		if (rows[i].depth == 0 && x >= BAR_W - CLOSE_W - PAD) {
			close_armed = n;
			return true;
		}

		if (n->is_dir) project_toggle(n);
		else           vng_tab_open(n->path);
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_UP: {
		if (!close_armed) return false;

		int i = row_at(e->button.x - ox, e->button.y);
		if (i >= 0 && rows[i].n == close_armed && rows[i].depth == 0 &&
		    e->button.x - ox >= BAR_W - CLOSE_W - PAD)
			project_remove(close_armed);

		close_armed = NULL;
		return true;
	}

	/* Motion is never consumed. The sidebar needs none of it - hover reads the pointer
	 * directly when drawing - and swallowing it would kill any drag that began on the
	 * sheet and crossed the panel: a resize grip pulled leftward, or a pan. */

	default:
		return false;
	}
}

void sidebar_draw (void)
{
	/* The clock runs whether the panel shows or not - this is the only place per frame
	 * that advances it, so an early return above it would freeze the slide half done. */
	anim_step();
	if (anim <= 0.0f) return;

	float ox = slide();

	rows_build();
	scroll_clamp();

	float mx, my;
	SDL_GetMouseState(&mx, &my);
	float rx = mx - ox;   /* the pointer, measured from the panel's own left edge */

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	float top = top_y();

	SDL_FRect panel = { ox, top, BAR_W, vng_win_h - top };
	SDL_SetRenderDrawColor(vng_ren, 0x14, 0x14, 0x14, 0xF0);
	SDL_RenderFillRect(vng_ren, &panel);

	/* A line down the right edge. The panel is translucent over a checkerboard, and
	 * without it the two greys blur into each other exactly where the edge should be. */
	SDL_FRect edge = { ox + BAR_W - 1.0f, top, 1.0f, vng_win_h - top };
	SDL_SetRenderDrawColor(vng_ren, 0x30, 0x30, 0x30, 0xFF);
	SDL_RenderFillRect(vng_ren, &edge);

	if (!vng_text) return;

	if (row_lot == 0) {
		text_print(vng_text, ox + PAD, top + PAD, 0x707070FF, "drop a folder here");
		return;
	}

	float h = row_h();

	for (int i = 0; i < row_lot; i++) {
		float y = row_y(i);
		/* A row scrolled half under the bar is drawn whole and then painted over:
		 * tabbar_draw runs after this, and its strip is opaque across the width. */
		if (y + h < top) continue;
		if (y > vng_win_h) break;

		VNG_NODE *n   = rows[i].n;
		float     ind = PAD + rows[i].depth * INDENT;
		bool      hot = (rx >= 0.0f && rx < BAR_W && my >= y && my < y + h);

		/* The file that is open right now is named in white. In a folder of thirty
		 * sprites, finding which one is on screen is otherwise a matter of reading the
		 * window title and then reading the list. */
		bool current = (!n->is_dir && vng_tab && vng_tab->path &&
		                SDL_strcmp(vng_tab->path, n->path) == 0);

		if (hot || current) {
			SDL_FRect r = { ox, y, BAR_W - 1.0f, h };
			SDL_SetRenderDrawColor(vng_ren, 0x2E, 0x2E, 0x2E, current ? 0xFF : 0x80);
			SDL_RenderFillRect(vng_ren, &r);
		}

		/* How much room the name has before it would run under the panel edge - or
		 * under the [x], on a root. The [x] only appears on hover, but the space is
		 * reserved whether it is showing or not: a name that fits until the pointer
		 * arrives and then gets overwritten is worse than a name that is always cut. */
		float name_x = ind + 12.0f;
		float room   = BAR_W - PAD - name_x - (rows[i].depth == 0 ? CLOSE_W + PAD : 0.0f);

		char label[160];
		text_fit(vng_text, label, sizeof label, n->name, room);

		if (n->is_dir) {
			text_print(vng_text, ox + ind, y + 1.0f, 0x707070FF, n->open ? "v" : ">");
			text_print(vng_text, ox + name_x, y + 1.0f,
			           rows[i].depth == 0 ? 0xDCDCDCFF : 0xB4B4B4FF, "%s", label);
		} else {
			text_print(vng_text, ox + name_x, y + 1.0f,
			           current ? 0xFFFFFFFF : 0x909090FF, "%s", label);
		}

		if (rows[i].depth == 0 && hot) {
			bool over = rx >= BAR_W - CLOSE_W - PAD;
			text_print(vng_text, ox + BAR_W - CLOSE_W - PAD + 2.0f, y + 1.0f,
			           over ? 0xFF6060FF : 0x707070FF, "x");
		}
	}
}
