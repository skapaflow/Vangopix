#include "thumb.h"
#include "view.h"
#include "core.h"
#include "win.h"

/* What it opens at, and the smallest it can be pulled to. The opening size is a whole 128
 * sprite - the largest size anybody calls small - and the floor is small enough to be parked
 * beside a character without covering it. */
#define OPEN_SIDE  128.0f
#define MIN_SIDE    32.0f
#define MARGIN       8.0f

static VNG_WIN *win = NULL;

/*
 * The part of the document the panel shows: as much as fits its interior at 1:1, centred on
 * whatever is at the middle of the main window, then clamped so it never runs off the sheet.
 *
 * THE CLAMP IS WHY THE ORIGINAL'S OUTLINE LIED. gui_thumbnail.c drew the region marker at the
 * centre of the screen, which is only where this rectangle is when nothing has been clamped -
 * so along every edge of every image the panel showed one thing and the marker pointed at
 * another. Everything below is computed from this one rectangle, so there is nothing left to
 * disagree.
 */
static SDL_FRect source (VNG_TAB *t, SDL_FRect area)
{
	float aw = area.w < (float)t->w ? area.w : (float)t->w;
	float ah = area.h < (float)t->h ? area.h : (float)t->h;

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

/* Where that rectangle lands inside the interior. A document smaller than the panel is
 * CENTRED rather than pinned to a corner: the panel is showing the whole thing, and a whole
 * thing sitting in the top left of a box reads as a thing that has been cut off. */
static SDL_FRect placed (SDL_FRect area, SDL_FRect src)
{
	SDL_FRect r = { area.x + SDL_floorf((area.w - src.w) * 0.5f),
	                area.y + SDL_floorf((area.h - src.h) * 0.5f),
	                src.w, src.h };
	return r;
}

static void body (SDL_FRect area, void *ctx)
{
	(void)ctx;

	VNG_TAB *t = vng_tab;
	if (!t || !t->tex) return;

	SDL_FRect src = source(t, area);
	SDL_FRect dst = placed(area, src);

	/* The desk first, so a document with alpha reads as transparent here exactly as it does
	 * on the sheet. The original drew nothing behind, and a sprite with a hole in it showed
	 * whatever happened to be under the window. */
	vangopix_desk_rect(dst);

	/* NEAREST is not a choice: at 1:1 there is nothing to filter, and the moment the panel is
	 * showing what the art really looks like, a filtered copy would be showing something
	 * else. */
	SDL_SetTextureScaleMode(t->tex, SDL_SCALEMODE_NEAREST);
	SDL_RenderTexture(vng_ren, t->tex, &src, &dst);
}

void thumb_toggle (void)
{
	if (win) { win_show(win, !win_visible(win)); return; }

	/* Opened in the bottom right the first time and never again: after that it is wherever it
	 * was left, which is the whole reason it is a window. Parked beside the character being
	 * drawn is what it is for. */
	SDL_FRect  a = { vng_win_w - MARGIN - OPEN_SIDE,
	                 vng_win_h - MARGIN - OPEN_SIDE, OPEN_SIDE, OPEN_SIDE };
	SDL_FPoint m = { MIN_SIDE, MIN_SIDE };

	win = win_open("1:1", a, m, body, NULL);
}

bool thumb_visible (void) { return win_visible(win); }

void thumb_draw (VNG_TAB *t)
{
	/*
	 * Only the marker on the SHEET is drawn here - the panel itself is drawn by win.c, inside
	 * the frame and clipped to it. The two halves come from the same source() call, which is
	 * what keeps them from ever pointing at different things.
	 *
	 * It is up only while the pointer is over the window, and only when the panel is showing
	 * PART of the document: a marker around the whole sheet tells nobody anything.
	 */
	if (!t || !win_visible(win) || !win_hover(win)) return;

	SDL_FRect src = source(t, win_area(win));
	if (src.w >= (float)t->w && src.h >= (float)t->h) return;

	SDL_FPoint a = view_world_to_screen(t, src.x, src.y);
	SDL_FPoint b = view_world_to_screen(t, src.x + src.w, src.y + src.h);

	SDL_FRect in  = { a.x, a.y, b.x - a.x, b.y - a.y };
	SDL_FRect out = { in.x - 1.0f, in.y - 1.0f, in.w + 2.0f, in.h + 2.0f };

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xC0);
	SDL_RenderRect(vng_ren, &out);
	SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xE0);
	SDL_RenderRect(vng_ren, &in);
}
