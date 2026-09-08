#include "colour.h"
#include "win.h"
#include "tool.h"
#include "core.h"

#define OPEN_W  200.0f
#define OPEN_H  180.0f
#define MIN_W   150.0f
#define MIN_H   130.0f

#define BAR_W    14.0f    /* the hue and alpha bars */
#define GAP       6.0f
#define SWATCH   16.0f
#define SLOTS     16      /* the "eight to sixteen", said back */

/* The gradients are generated small and stretched. A hue ramp and a saturation-value field
 * are smooth by definition, so there is nothing for more pixels to say - and 64 is enough
 * that the linear filter has no visible steps to smooth over. */
#define RAMP  64

static VNG_WIN *win = NULL;

/* The picker's own state. HSV and not RGB, because the square and the bar ARE hue,
 * saturation and value - keeping RGB and converting both ways every frame would make a
 * grey ambiguous, since every hue is the same grey and the bar would jump. */
static float h = 0.0f, s = 1.0f, v = 1.0f;
static Uint8 alpha = 0xFF;

static int slot = 0;              /* which of the two is being edited */
static Uint32 last = 0xFF000000u; /* what this window last wrote, to notice outside changes */

static Uint32 swatch[SLOTS];

static SDL_Texture *tex_sv  = NULL;   /* rebuilt when the hue moves */
static SDL_Texture *tex_hue = NULL;   /* built once */
static SDL_Texture *tex_a   = NULL;   /* rebuilt when the colour moves */
static float sv_hue = -1.0f;
static Uint32 a_of  = 1u;

/* ------------------------------------------------------------------------- the maths */

static Uint32 hsv_argb (float hh, float ss, float vv, Uint8 aa)
{
	float c = vv * ss;
	float x = c * (1.0f - SDL_fabsf(SDL_fmodf(hh / 60.0f, 2.0f) - 1.0f));
	float m = vv - c;

	float r = 0, g = 0, b = 0;
	if      (hh <  60.0f) { r = c; g = x; }
	else if (hh < 120.0f) { r = x; g = c; }
	else if (hh < 180.0f) { g = c; b = x; }
	else if (hh < 240.0f) { g = x; b = c; }
	else if (hh < 300.0f) { r = x; b = c; }
	else                  { r = c; b = x; }

	return ((Uint32)aa << 24)
	     | ((Uint32)((r + m) * 255.0f + 0.5f) << 16)
	     | ((Uint32)((g + m) * 255.0f + 0.5f) <<  8)
	     |  (Uint32)((b + m) * 255.0f + 0.5f);
}

static void argb_hsv (Uint32 c, float *hh, float *ss, float *vv)
{
	float r = (float)((c >> 16) & 0xFF) / 255.0f;
	float g = (float)((c >>  8) & 0xFF) / 255.0f;
	float b = (float)( c        & 0xFF) / 255.0f;

	float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
	float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
	float d  = mx - mn;

	*vv = mx;
	*ss = mx > 0.0f ? d / mx : 0.0f;

	/* A grey has no hue, and inventing one would swing the bar every time somebody picked
	 * black. The hue in hand is kept instead. */
	if (d <= 0.0f) return;

	if      (mx == r) *hh = 60.0f * SDL_fmodf((g - b) / d, 6.0f);
	else if (mx == g) *hh = 60.0f * ((b - r) / d + 2.0f);
	else              *hh = 60.0f * ((r - g) / d + 4.0f);

	if (*hh < 0.0f) *hh += 360.0f;
}

/* ---------------------------------------------------------------------- the gradients */

static SDL_Texture *ramp_make (int w, int hgt, const Uint32 *px)
{
	SDL_Texture *t = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
	                                   SDL_TEXTUREACCESS_STATIC, w, hgt);
	if (!t) return NULL;

	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
	SDL_UpdateTexture(t, NULL, px, w * (int)sizeof(Uint32));
	return t;
}

static void build (void)
{
	static Uint32 px[RAMP * RAMP];

	if (!tex_hue) {
		for (int y = 0; y < RAMP; y++)
			px[y] = hsv_argb((float)y * 360.0f / RAMP, 1.0f, 1.0f, 0xFF);
		tex_hue = ramp_make(1, RAMP, px);
	}

	if (sv_hue != h) {
		for (int y = 0; y < RAMP; y++)
			for (int x = 0; x < RAMP; x++)
				px[y * RAMP + x] = hsv_argb(h, (float)x / (RAMP - 1),
				                            1.0f - (float)y / (RAMP - 1), 0xFF);
		if (tex_sv) SDL_DestroyTexture(tex_sv);
		tex_sv = ramp_make(RAMP, RAMP, px);
		sv_hue = h;
	}

	Uint32 solid = hsv_argb(h, s, v, 0xFF);
	if (a_of != solid) {
		for (int y = 0; y < RAMP; y++)
			px[y] = (solid & 0x00FFFFFFu)
			      | ((Uint32)(255 - y * 255 / (RAMP - 1)) << 24);
		if (tex_a) SDL_DestroyTexture(tex_a);
		tex_a = ramp_make(1, RAMP, px);
		a_of = solid;
	}
}

void colour_free (void)
{
	if (tex_sv)  SDL_DestroyTexture(tex_sv);
	if (tex_hue) SDL_DestroyTexture(tex_hue);
	if (tex_a)   SDL_DestroyTexture(tex_a);
	tex_sv = tex_hue = tex_a = NULL;
	sv_hue = -1.0f;
	a_of   = 1u;
}

/* ------------------------------------------------------------------------ the layout */

/* Everything is measured from the interior, so stretching the window stretches the picker
 * rather than leaving it in a corner. */
typedef struct { SDL_FRect sv, hue, a, slots, tiles; } LAYOUT;

static LAYOUT layout (SDL_FRect r)
{
	LAYOUT l;

	float rows  = SDL_ceilf(SLOTS / 8.0f);
	float bottom = SWATCH * rows + GAP + (vng_text ? text_line_height(vng_text) : 15.0f);

	float top_h = r.h - bottom - GAP;
	if (top_h < 24.0f) top_h = 24.0f;

	l.sv  = (SDL_FRect){ r.x, r.y, r.w - (BAR_W + GAP) * 2.0f, top_h };
	l.hue = (SDL_FRect){ l.sv.x + l.sv.w + GAP, r.y, BAR_W, top_h };
	l.a   = (SDL_FRect){ l.hue.x + BAR_W + GAP, r.y, BAR_W, top_h };

	l.slots = (SDL_FRect){ r.x, r.y + top_h + GAP, SWATCH * 2.0f + GAP,
	                       (vng_text ? text_line_height(vng_text) : 15.0f) };
	l.tiles = (SDL_FRect){ r.x, r.y + r.h - SWATCH * rows, SWATCH * 8.0f, SWATCH * rows };
	return l;
}

static bool in_rect (SDL_FRect r, float x, float y)
{
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static float clamp01 (float f) { return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); }

/* ------------------------------------------------------------------------ the events */

/* What the window is holding on to between the press and the release. */
static enum { GRAB_NONE, GRAB_SV, GRAB_HUE, GRAB_A } grab = GRAB_NONE;

static void push (void)
{
	last = hsv_argb(h, s, v, alpha);
	tool_set_colour(slot, last);
}

static bool on_event (SDL_FRect area, const SDL_Event *e, void *ctx)
{
	(void)ctx;

	LAYOUT l = layout(area);

	float x, y;
	if (e->type == SDL_EVENT_MOUSE_MOTION) { x = e->motion.x; y = e->motion.y; }
	else                                   { x = e->button.x; y = e->button.y; }

	if (e->type == SDL_EVENT_MOUSE_BUTTON_UP) { grab = GRAB_NONE; return false; }

	if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (in_rect(l.sv, x, y))       grab = GRAB_SV;
		else if (in_rect(l.hue, x, y)) grab = GRAB_HUE;
		else if (in_rect(l.a, x, y))   grab = GRAB_A;
		else {
			/* The two slots: clicking one says which is being edited, and the picker jumps
			 * to the colour that is in it rather than overwriting it. */
			SDL_FRect s0 = { l.slots.x, l.slots.y, SWATCH, l.slots.h };
			SDL_FRect s1 = { l.slots.x + SWATCH + GAP, l.slots.y, SWATCH, l.slots.h };

			if (in_rect(s0, x, y) || in_rect(s1, x, y)) {
				slot = in_rect(s1, x, y) ? 1 : 0;
				last = ~tool_colour(slot);   /* forces the follow below */
				return false;
			}

			if (in_rect(l.tiles, x, y)) {
				int col = (int)((x - l.tiles.x) / SWATCH);
				int row = (int)((y - l.tiles.y) / SWATCH);
				int i   = row * 8 + col;

				if (i >= 0 && i < SLOTS) {
					/* SHIFT stores, a bare click loads. Storing is the rarer of the two and
					 * takes the modifier, which is the way round that keeps the common
					 * gesture free. */
					if (SDL_GetModState() & SDL_KMOD_SHIFT)
						swatch[i] = hsv_argb(h, s, v, alpha);
					else {
						/* An empty tile loads as nothing, and nothing is a colour here -
						 * the second slot starts as it. There is no "unset" to guard for. */
						argb_hsv(swatch[i], &h, &s, &v);
						alpha = (Uint8)((swatch[i] >> 24) & 0xFF);
						push();
					}
				}
				return false;
			}
			return false;
		}
	}

	if (grab == GRAB_NONE) return false;

	switch (grab) {
	case GRAB_SV:
		s = clamp01((x - l.sv.x) / l.sv.w);
		v = 1.0f - clamp01((y - l.sv.y) / l.sv.h);
		break;
	case GRAB_HUE:
		h = clamp01((y - l.hue.y) / l.hue.h) * 359.99f;
		break;
	case GRAB_A:
		alpha = (Uint8)((1.0f - clamp01((y - l.a.y) / l.a.h)) * 255.0f + 0.5f);
		break;
	default: break;
	}

	push();
	return true;   /* the drag is ours until the button comes up */
}

/* ------------------------------------------------------------------------- the pixels */

static void marker (float x, float y)
{
	SDL_FRect in  = { x - 3.0f, y - 3.0f, 7.0f, 7.0f };
	SDL_FRect out = { x - 4.0f, y - 4.0f, 9.0f, 9.0f };

	/* Black then white, the rule every marker in this program follows: one tone alone
	 * disappears against something. */
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xC0);
	SDL_RenderRect(vng_ren, &out);
	SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xF0);
	SDL_RenderRect(vng_ren, &in);
}

/* A colour over the desk at its real alpha, which is how every swatch in this program shows
 * one - the bars at the bottom of the screen do the same. */
static void chip (SDL_FRect r, Uint32 c)
{
	vangopix_desk_rect(r);

	SDL_SetRenderDrawColor(vng_ren, (Uint8)((c >> 16) & 0xFF), (Uint8)((c >> 8) & 0xFF),
	                                (Uint8)(c & 0xFF), (Uint8)((c >> 24) & 0xFF));
	SDL_RenderFillRect(vng_ren, &r);

	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0x90);
	SDL_RenderRect(vng_ren, &r);
}

static void body (SDL_FRect area, void *ctx)
{
	(void)ctx;

	/*
	 * THE WINDOW FOLLOWS THE SLOT, it does not own it. A colour absorbed with CTRL out on the
	 * sheet lands in the slot, and the picker notices here rather than being told - which is
	 * what keeps one truth about what the colour is instead of two that drift.
	 */
	Uint32 now = tool_colour(slot);
	if (now != last) {
		argb_hsv(now, &h, &s, &v);
		alpha = (Uint8)((now >> 24) & 0xFF);
		last  = now;
	}

	build();

	LAYOUT l = layout(area);

	if (tex_sv)  SDL_RenderTexture(vng_ren, tex_sv,  NULL, &l.sv);
	if (tex_hue) SDL_RenderTexture(vng_ren, tex_hue, NULL, &l.hue);
	if (tex_a) {
		vangopix_desk_rect(l.a);
		SDL_RenderTexture(vng_ren, tex_a, NULL, &l.a);
	}

	marker(l.sv.x + s * l.sv.w, l.sv.y + (1.0f - v) * l.sv.h);
	marker(l.hue.x + BAR_W * 0.5f, l.hue.y + (h / 360.0f) * l.hue.h);
	marker(l.a.x + BAR_W * 0.5f, l.a.y + (1.0f - alpha / 255.0f) * l.a.h);

	/* The two slots, with the one being edited ringed. */
	for (int i = 0; i < 2; i++) {
		SDL_FRect r = { l.slots.x + i * (SWATCH + GAP), l.slots.y, SWATCH, l.slots.h };
		chip(r, tool_colour(i));

		if (i == slot) {
			SDL_FRect o = { r.x - 2.0f, r.y - 2.0f, r.w + 4.0f, r.h + 4.0f };
			SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xE0);
			SDL_RenderRect(vng_ren, &o);
		}
	}

	if (vng_text) {
		char hex[16];
		tool_hex(tool_colour(slot), hex, sizeof hex);
		text_print(vng_text, l.slots.x + (SWATCH + GAP) * 2.0f + GAP, l.slots.y,
		           0xB4B4B4FF, "%s", hex);
	}

	for (int i = 0; i < SLOTS; i++) {
		SDL_FRect r = { l.tiles.x + (i % 8) * SWATCH, l.tiles.y + (i / 8) * SWATCH,
		                SWATCH - 1.0f, SWATCH - 1.0f };
		chip(r, swatch[i]);
	}
}

void colour_toggle (void)
{
	if (win) { win_show(win, !win_visible(win)); return; }

	SDL_FRect  a = { 24.0f, 48.0f, OPEN_W, OPEN_H };
	SDL_FPoint m = { MIN_W, MIN_H };

	win = win_open("colour", a, m, body, on_event, NULL);
}

bool colour_visible (void) { return win_visible(win); }
