#include "sidebar.h"
#include "project.h"
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

typedef struct { VNG_NODE *n; int depth; } ROW;

static ROW rows[MAX_ROWS];
static int row_lot = 0;

/* The root whose [x] the button went down on. Same contract as the tab bar: the removal
 * only fires if the button comes back up over the same [x]. */
static VNG_NODE *close_armed = NULL;

void sidebar_toggle  (void) { visible = !visible; }
bool sidebar_visible (void) { return visible; }

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
	float over = content_h() - (vng_win_h - PAD * 2.0f);
	if (over < 0.0f) over = 0.0f;
	if (scroll > over)  scroll = over;
	if (scroll < 0.0f)  scroll = 0.0f;
}

static int row_at (float x, float y)
{
	if (x < 0.0f || x >= BAR_W) return -1;

	float local = y - PAD + scroll;
	if (local < 0.0f) return -1;

	int i = (int)(local / row_h());
	return (i >= 0 && i < row_lot) ? i : -1;
}

static float row_y (int i) { return PAD + i * row_h() - scroll; }

bool sidebar_event (const SDL_Event *e)
{
	if (!visible) return false;

	rows_build();

	switch (e->type) {

	case SDL_EVENT_MOUSE_WHEEL: {
		/* Only when the pointer is over the panel. Everywhere else the wheel is the
		 * camera's, and stealing it would make zooming stop working near the left edge
		 * of the window for no reason a person could see. */
		if (e->wheel.mouse_x >= BAR_W) return false;

		scroll -= e->wheel.integer_y * row_h() * 3.0f;
		scroll_clamp();
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		float x = e->button.x, y = e->button.y;
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

		int i = row_at(e->button.x, e->button.y);
		if (i >= 0 && rows[i].n == close_armed && rows[i].depth == 0 &&
		    e->button.x >= BAR_W - CLOSE_W - PAD)
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
	if (!visible) return;

	rows_build();
	scroll_clamp();

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	SDL_FRect panel = { 0.0f, 0.0f, BAR_W, (float)vng_win_h };
	SDL_SetRenderDrawColor(vng_ren, 0x14, 0x14, 0x14, 0xF0);
	SDL_RenderFillRect(vng_ren, &panel);

	/* A line down the right edge. The panel is translucent over a checkerboard, and
	 * without it the two greys blur into each other exactly where the edge should be. */
	SDL_FRect edge = { BAR_W - 1.0f, 0.0f, 1.0f, (float)vng_win_h };
	SDL_SetRenderDrawColor(vng_ren, 0x30, 0x30, 0x30, 0xFF);
	SDL_RenderFillRect(vng_ren, &edge);

	if (!vng_text) return;

	if (row_lot == 0) {
		text_print(vng_text, PAD, PAD, 0x707070FF, "drop a folder here");
		return;
	}

	float h = row_h();

	for (int i = 0; i < row_lot; i++) {
		float y = row_y(i);
		if (y + h < 0.0f) continue;
		if (y > vng_win_h) break;

		VNG_NODE *n   = rows[i].n;
		float     ind = PAD + rows[i].depth * INDENT;
		bool      hot = (mx < BAR_W && my >= y && my < y + h);

		/* The file that is open right now is named in white. In a folder of thirty
		 * sprites, finding which one is on screen is otherwise a matter of reading the
		 * window title and then reading the list. */
		bool current = (!n->is_dir && vng_tab && vng_tab->path &&
		                SDL_strcmp(vng_tab->path, n->path) == 0);

		if (hot || current) {
			SDL_FRect r = { 0.0f, y, BAR_W - 1.0f, h };
			SDL_SetRenderDrawColor(vng_ren, 0x2E, 0x2E, 0x2E, current ? 0xFF : 0x80);
			SDL_RenderFillRect(vng_ren, &r);
		}

		if (n->is_dir) {
			text_print(vng_text, ind, y + 1.0f, 0x707070FF, n->open ? "v" : ">");
			text_print(vng_text, ind + 12.0f, y + 1.0f,
			           rows[i].depth == 0 ? 0xDCDCDCFF : 0xB4B4B4FF, "%s", n->name);
		} else {
			text_print(vng_text, ind + 12.0f, y + 1.0f,
			           current ? 0xFFFFFFFF : 0x909090FF, "%s", n->name);
		}

		if (rows[i].depth == 0 && hot) {
			bool over = mx >= BAR_W - CLOSE_W - PAD;
			text_print(vng_text, BAR_W - CLOSE_W - PAD + 2.0f, y + 1.0f,
			           over ? 0xFF6060FF : 0x707070FF, "x");
		}
	}
}
