#include "tabbar.h"
#include "ui.h"
#include "file.h"
#include "tabs.h"

#define BAR_H     (ui_row() + ui_pad())
#define TAB_MIN   (ui_cell() * 8.0f)     /* below this a name is unreadable and the bar is useless */
#define TAB_MAX   (ui_cell() * 22.0f)     /* above this two tabs look like a menu, not like tabs     */
#define PLUS_W    26.0f     /* the [+] that opens a fresh sheet                        */
#define CLOSE_W   ui_close()
#define PAD       (ui_pad() + 3.0f)
#define DRAG_SLOP  4.0f     /* travel before a click becomes a drag                    */

static bool visible = false;

/* The drag is two states, not one. `held` is the tab the button went down on, which is
 * still only a click; `dragging` is that click having travelled past the slop. Without
 * the split, every click on a tab would reorder by a pixel of hand tremor. */
static VNG_TAB *held     = NULL;
static bool     dragging = false;
static float    press_x  = 0.0f;
static float    grab_dx  = 0.0f;   /* where inside the tab the hand took hold */

/* The tab the button went down on over its [x]. The close only fires if the button
 * comes back up over the same [x] - the same contract every button on every desktop
 * has, and what lets a mis-press be taken back by sliding off before releasing. */
static VNG_TAB *close_armed = NULL;

void tabbar_toggle  (void) { visible = !visible; }
bool tabbar_visible (void) { return visible; }
float tabbar_height (void) { return visible ? BAR_H : 0.0f; }

static float tab_w (void)
{
	int n = vng_tab_count();
	if (n < 1) n = 1;

	float w = (vng_win_w - PLUS_W) / (float)n;
	if (w > TAB_MAX) w = TAB_MAX;
	if (w < TAB_MIN) w = TAB_MIN;   /* past this they overflow the window, and that is
	                                 * better than a row of unreadable stubs */
	return w;
}

/* Where tab number i sits when nothing is being dragged. */
static float slot_x (int i) { return i * tab_w(); }

static VNG_TAB *tab_at (float x, float y, int *out_index, bool *out_on_close)
{
	if (y < 0 || y >= BAR_H) return NULL;

	float w = tab_w();
	int   i = 0;

	for (VNG_TAB *p = vng_tabs; p; p = p->next, i++) {
		float x0 = slot_x(i);
		if (x >= x0 && x < x0 + w) {
			if (out_index)    *out_index    = i;
			if (out_on_close) *out_on_close = (x >= x0 + w - CLOSE_W - PAD * 0.5f);
			return p;
		}
	}
	return NULL;
}

static bool on_plus (float x, float y)
{
	if (y < 0 || y >= BAR_H) return false;
	float x0 = slot_x(vng_tab_count());
	return x >= x0 && x < x0 + PLUS_W;
}

bool tabbar_event (const SDL_Event *e)
{
	if (!visible) return false;

	switch (e->type) {

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		if (e->button.button != SDL_BUTTON_LEFT) return false;

		float x = e->button.x, y = e->button.y;

		if (on_plus(x, y)) {
			vng_tab_new(VNG_NEW_W, VNG_NEW_H);
			return true;
		}

		int  index = 0;
		bool on_close = false;
		VNG_TAB *t = tab_at(x, y, &index, &on_close);
		if (!t) return y < BAR_H;   /* a click on the empty part of the bar is still
		                             * the bar's, or it would fall through to the sheet */

		if (on_close) {
			close_armed = t;
			return true;
		}

		/* Selecting happens on press, not on release: the tab has to be the current
		 * one before the drag starts, or dragging would reorder a document that is
		 * not the one on screen. */
		vng_tab_show(t);

		held     = t;
		dragging = false;
		press_x  = x;
		grab_dx  = x - slot_x(index);
		return true;
	}

	case SDL_EVENT_MOUSE_MOTION: {
		if (!held) return false;

		/* The held tab can be destroyed under the hand - CTRL+W is still live while the
		 * button is down, and the close frees it. vng_tab_index walks the list and
		 * compares pointers without dereferencing the one it is looking for, so asking
		 * whether the tab is still in the row is safe even once it is not. Without this
		 * the next motion event reorders freed memory. */
		if (vng_tab_index(held) == 0) {
			held     = NULL;
			dragging = false;
			return false;
		}

		float x = e->motion.x;

		if (!dragging && SDL_fabsf(x - press_x) >= DRAG_SLOP)
			dragging = true;
		if (!dragging) return true;

		/* The target slot is decided by where the CENTRE of the dragged tab lands, not
		 * by the cursor: dragging by the right edge would otherwise insert one slot
		 * early, and the tab would appear to jump out from under the hand. */
		float w      = tab_w();
		float left   = x - grab_dx;
		int   target = (int)SDL_floorf((left + w * 0.5f) / w);

		vng_tab_move(held, target);
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_UP: {
		if (e->button.button != SDL_BUTTON_LEFT) return false;

		bool consumed = (held != NULL) || (close_armed != NULL);

		if (close_armed && vng_tab_index(close_armed) == 0)
			close_armed = NULL;   /* closed by other means while the button was down */

		if (close_armed) {
			bool on_close = false;
			VNG_TAB *t = tab_at(e->button.x, e->button.y, NULL, &on_close);
			if (t == close_armed && on_close)
				/* Through file_close_tab and not vng_tab_close: the question about
				 * unsaved work is asked in one place, whichever way the tab is shut. */
				file_close_tab(close_armed);
			close_armed = NULL;
		}

		held     = NULL;
		dragging = false;
		return consumed;
	}

	default:
		return false;
	}
}

void tabbar_draw (void)
{
	if (!visible) return;

	float w  = tab_w();
	float mx = 0.0f, my = 0.0f;
	SDL_GetMouseState(&mx, &my);

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	/* The strip behind the tabs is translucent, not opaque: the bar floats over the
	 * sheet, and letting the drawing show through is what keeps it reading as
	 * something summoned rather than something the window always had. */
	SDL_FRect strip = { 0.0f, 0.0f, (float)vng_win_w, BAR_H };
	SDL_SetRenderDrawColor(vng_ren, 0x14, 0x14, 0x14, 0xE0);
	SDL_RenderFillRect(vng_ren, &strip);

	int i = 0;
	for (VNG_TAB *p = vng_tabs; p; p = p->next, i++) {

		float x = slot_x(i);

		/* The dragged tab follows the hand. It has already been relinked into its new
		 * slot by then, so this offset is only the distance between the slot it now
		 * owns and where the hand actually is. */
		if (dragging && p == held) {
			x = mx - grab_dx;
			if (x < 0) x = 0;
			if (x > vng_win_w - w) x = vng_win_w - w;
		}

		bool active = (p == vng_tab);

		SDL_FRect r = { x + 1.0f, 1.0f, w - 2.0f, BAR_H - 1.0f };
		SDL_SetRenderDrawColor(vng_ren,
		                       active ? 0x2E : 0x1A, active ? 0x2E : 0x1A,
		                       active ? 0x2E : 0x1A, 0xFF);
		SDL_RenderFillRect(vng_ren, &r);

		/* The active tab is marked by a line on top rather than by colour alone -
		 * colour alone disappears for anyone who cannot separate two dark greys. */
		if (active) {
			SDL_FRect bar = { x + 1.0f, 0.0f, w - 2.0f, 2.0f };
			SDL_SetRenderDrawColor(vng_ren, 0x4C, 0x9A, 0xFF, 0xFF);
			SDL_RenderFillRect(vng_ren, &bar);
		}

		char label[80];
		text_fit(vng_text, label, sizeof label, p->name, w - CLOSE_W - PAD * 2.0f);
		text_print(vng_text, x + PAD, 5.0f,
		           active ? 0xFFFFFFFF : 0x909090FF, "%s", label);

		/* The dirty marker takes the [x]'s place until the pointer comes near, so the
		 * two never fight for the same corner. */
		float cx = x + w - CLOSE_W - PAD * 0.5f;
		bool  hot = (my >= 0 && my < BAR_H && mx >= x && mx < x + w);

		if (hot) {
			bool over = mx >= cx;
			text_print(vng_text, cx + 3.0f, 5.0f,
			           over ? 0xFF6060FF : 0x909090FF, "x");
		} else if (p->dirty) {
			text_print(vng_text, cx + 3.0f, 5.0f, 0x909090FF, "*");
		}
	}

	/* [+] - a new sheet, the same thing CTRL+N does. It exists because the bar is
	 * where a hand already is when it is thinking about documents. */
	float px = slot_x(i);
	bool  plus_hot = on_plus(mx, my);

	SDL_FRect pr = { px + 1.0f, 1.0f, PLUS_W - 2.0f, BAR_H - 1.0f };
	SDL_SetRenderDrawColor(vng_ren, 0x1A, 0x1A, 0x1A, 0xFF);
	SDL_RenderFillRect(vng_ren, &pr);
	text_print(vng_text, px + 9.0f, 5.0f, plus_hot ? 0xFFFFFFFF : 0x909090FF, "+");
}
