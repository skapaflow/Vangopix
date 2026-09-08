#include "thumb.h"
#include "view.h"
#include "core.h"

/* The biggest the panel gets, in document pixels. Chosen so a 128 sprite - the largest size
 * anybody calls small - fits whole with room to spare, and so the panel stays a corner of the
 * window rather than a second view of it. */
#define MAX_SIDE  160.0f

#define MARGIN    8.0f

static bool visible = false;

void thumb_toggle  (void) { visible = !visible; }
bool thumb_visible (void) { return visible; }

/*
 * How much document the panel shows: its own size, or the whole document when that is
 * smaller.
 *
 * The second half matters more than it looks. A 16x16 sprite in a fixed 160x160 panel would
 * be a speck in a corner surrounded by nothing; shrinking the panel to the art means the
 * panel IS the art, at the size it will be seen. That is the whole point of the thing.
 */
static void area (VNG_TAB *t, float *w, float *h)
{
	*w = (float)t->w < MAX_SIDE ? (float)t->w : MAX_SIDE;
	*h = (float)t->h < MAX_SIDE ? (float)t->h : MAX_SIDE;
}

/*
 * The part of the document the panel shows: centred on whatever is at the middle of the main
 * window, then clamped so it never runs off the sheet.
 *
 * THE CLAMP IS WHY THE ORIGINAL'S OUTLINE LIED. It drew the marker at the centre of the
 * screen, which is only where this rectangle is when nothing has been clamped - so along
 * every edge of every image the panel showed one thing and the marker pointed at another.
 * Everything below is computed from this one rectangle, so there is nothing left to disagree.
 */
static SDL_FRect source (VNG_TAB *t)
{
	float aw, ah;
	area(t, &aw, &ah);

	SDL_FPoint c = view_screen_to_world(t, vng_win_w * 0.5f, vng_win_h * 0.5f);

	float x = SDL_floorf(c.x) - aw * 0.5f;
	float y = SDL_floorf(c.y) - ah * 0.5f;

	if (x > (float)t->w - aw) x = (float)t->w - aw;
	if (y > (float)t->h - ah) y = (float)t->h - ah;
	if (x < 0.0f) x = 0.0f;
	if (y < 0.0f) y = 0.0f;

	SDL_FRect r = { x, y, aw, ah };
	return r;
}

static SDL_FRect panel (VNG_TAB *t)
{
	float aw, ah;
	area(t, &aw, &ah);

	SDL_FRect r = { vng_win_w - MARGIN - aw, vng_win_h - MARGIN - ah, aw, ah };
	return r;
}

static bool over (VNG_TAB *t, float mx, float my)
{
	SDL_FRect p = panel(t);
	return mx >= p.x && my >= p.y && mx < p.x + p.w && my < p.y + p.h;
}

bool thumb_event (const SDL_Event *e, VNG_TAB *t)
{
	if (!visible || !t) return false;

	/* Only presses. Motion is deliberately NOT consumed, for the same reason the sidebar
	 * does not consume it: a drag begun on the sheet that crosses this panel - a stroke, a
	 * pan, a grip pulled across - must not be cut in half by it. */
	switch (e->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return over(t, e->button.x, e->button.y);
	case SDL_EVENT_MOUSE_WHEEL:
		return over(t, e->wheel.mouse_x, e->wheel.mouse_y);
	default:
		return false;
	}
}

void thumb_draw (VNG_TAB *t)
{
	if (!visible || !t || !t->tex) return;

	SDL_FRect src = source(t);
	SDL_FRect dst = panel(t);

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	/* The desk first, so a transparent document reads as transparent here exactly as it does
	 * on the sheet. The original drew nothing behind, and a sprite with alpha showed whatever
	 * had been under the window. */
	vangopix_desk_rect(dst);

	/* NEAREST is not a choice here: at 1:1 there is nothing to filter, and the moment the
	 * panel is showing what the art really looks like, a filtered copy would be showing
	 * something else. */
	SDL_SetTextureScaleMode(t->tex, SDL_SCALEMODE_NEAREST);
	SDL_RenderTexture(vng_ren, t->tex, &src, &dst);

	/* Outside the panel, never on it - the rule the grips, the sheet's frame and the tip
	 * outline all follow, and here it keeps the outermost row of the art visible. */
	SDL_FRect edge = { dst.x - 1.0f, dst.y - 1.0f, dst.w + 2.0f, dst.h + 2.0f };
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xFF);
	SDL_RenderRect(vng_ren, &edge);

	/*
	 * While the pointer is over the panel, the part of the sheet it is showing is outlined
	 * ON the sheet. That is what turns a readout into something the eye can follow: the two
	 * views say which part of one another they are.
	 *
	 * Drawn only when there is something to say - a panel showing the whole document would
	 * be outlining the whole document, which tells nobody anything.
	 */
	float mx, my;
	SDL_GetMouseState(&mx, &my);

	if (!over(t, mx, my)) return;
	if (src.w >= (float)t->w && src.h >= (float)t->h) return;

	SDL_FPoint a = view_world_to_screen(t, src.x, src.y);
	SDL_FPoint b = view_world_to_screen(t, src.x + src.w, src.y + src.h);

	SDL_FRect in  = { a.x, a.y, b.x - a.x, b.y - a.y };
	SDL_FRect out = { in.x - 1.0f, in.y - 1.0f, in.w + 2.0f, in.h + 2.0f };

	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xC0);
	SDL_RenderRect(vng_ren, &out);
	SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xE0);
	SDL_RenderRect(vng_ren, &in);
}
