#include "colour.h"
#include "win.h"
#include "tool.h"
#include "core.h"
#include "keys.h"

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

/*
 * THE HEX READOUT IS THE HEX FIELD. Leaving somewhere to TYPE a colour out was a mistake and
 * not a decision: a published palette arrives as a string - #2E3440, and fifteen more like it
 * - and with nowhere to put one the only way in is to paste the image and eyedrop it. The
 * first Vangopix had a text box for this and was right to.
 *
 * It is not a second control beside the readout, though. The readout already says what the
 * colour is; clicking it and typing over it is the same thing answering in both directions,
 * and one box is better than a label with a box under it.
 *
 * IT IS THE SECOND THING IN THE PROGRAM TO OWN THE KEYBOARD, after the CTRL+N prompt - which
 * is what keys.c was built for. While it is being typed into, TAB does not raise the sidebar
 * and Q does not take the pencil, for free.
 */
static bool editing = false;
static char field[12];

/*
 * THE FIRST KEY TYPED REPLACES WHAT IS THERE, and the rest add to it.
 *
 * The field opens holding the colour it is showing - it has to, or clicking it to check a
 * value would blank it. But the reason a person clicks it is almost always to put a
 * DIFFERENT colour in, and having to press backspace eight times first is the kind of thing
 * that makes a field not worth using. Every small value field in every program behaves this
 * way; backspace cancels it, because backspace means "I am editing this one" rather than
 * "I am replacing it".
 */
static bool fresh = false;

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
typedef struct { SDL_FRect sv, hue, a, slots, hex, tiles; } LAYOUT;

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

	float lh = vng_text ? text_line_height(vng_text) : 15.0f;

	l.slots = (SDL_FRect){ r.x, r.y + top_h + GAP, SWATCH * 2.0f + GAP, lh };
	l.hex   = (SDL_FRect){ l.slots.x + l.slots.w + GAP, l.slots.y,
	                       r.w - l.slots.w - GAP, lh };
	l.tiles = (SDL_FRect){ r.x, r.y + r.h - SWATCH * rows, SWATCH * 8.0f, SWATCH * rows };
	return l;
}

static bool in_rect (SDL_FRect r, float x, float y)
{
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static float clamp01 (float f) { return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); }

/* ------------------------------------------------------------------------- the field */

/*
 * Liberal in what it takes, because a colour is copied from somewhere else and arrives in
 * whatever shape that somewhere used: with or without the hash, three digits or six or eight.
 * Refusing "#2E3440" for its hash would be refusing the only form most palettes are published
 * in.
 *
 * Six digits means OPAQUE, which is what #RRGGBB means everywhere; eight says the alpha
 * outright. Three is the shorthand, each digit doubled.
 */
static bool hex_parse (const char *t, Uint32 *out)
{
	char d[9];
	int  n = 0;

	for (; *t && n < 8; t++) {
		if (*t == '#' || *t == ' ') continue;
		if (!SDL_isxdigit((unsigned char)*t)) return false;
		d[n++] = *t;
	}
	d[n] = 0;

	if (n != 3 && n != 6 && n != 8) return false;

	unsigned long v32 = SDL_strtoul(d, NULL, 16);

	if (n == 3) {
		unsigned r = (v32 >> 8) & 0xF, g = (v32 >> 4) & 0xF, b = v32 & 0xF;
		*out = 0xFF000000u | (r * 0x11u << 16) | (g * 0x11u << 8) | (b * 0x11u);
		return true;
	}
	if (n == 6) { *out = 0xFF000000u | (Uint32)v32; return true; }

	/* RRGGBBAA on the way in, because that is how it is shown - the shape a person can paste
	 * elsewhere. The document is 0xAARRGGBB and the two orders must not be confused. */
	*out = ((Uint32)(v32 & 0xFFu) << 24) | (Uint32)(v32 >> 8);
	return true;
}

static void field_stop (bool keep)
{
	if (!editing) return;
	editing = false;
	keys_release(&editing);

	Uint32 c;
	if (keep && hex_parse(field, &c)) {
		argb_hsv(c, &h, &s, &v);
		alpha = (Uint8)((c >> 24) & 0xFF);
		last  = c;
		tool_set_colour(slot, c);
	}
}

static void field_key (const SDL_Event *e, void *ctx)
{
	(void)ctx;

	if (e->type == SDL_EVENT_TEXT_INPUT) {
		if (fresh) { field[0] = 0; fresh = false; }

		for (const char *t = e->text.text; *t; t++)
			if (SDL_isxdigit((unsigned char)*t) && SDL_strlen(field) < 8) {
				size_t n = SDL_strlen(field);
				field[n]     = *t;
				field[n + 1] = 0;
			}
		return;
	}
	if (e->type != SDL_EVENT_KEY_DOWN) return;

	switch (e->key.key) {
	case SDLK_BACKSPACE: {
		size_t n = SDL_strlen(field);
		if (n) field[n - 1] = 0;
		fresh = false;
		break;
	}
	case SDLK_RETURN:
	case SDLK_KP_ENTER: field_stop(true);  break;
	case SDLK_ESCAPE:   field_stop(false); break;
	default: break;
	}
}

static void field_start (void)
{
	tool_hex(tool_colour(slot), field, sizeof field);
	editing = true;
	fresh   = true;
	keys_capture(field_key, &editing);
}

/* ------------------------------------------------------------------------ the events */

/* What the window is holding on to between the press and the release. */
static enum { GRAB_NONE, GRAB_SV, GRAB_HUE, GRAB_A } grab = GRAB_NONE;

/*
 * SHIFT CONSTRAINS THE SQUARE TO ONE AXIS, and that is the capability the first Vangopix's
 * four channel bars were really for.
 *
 * On a square, saturation and value move TOGETHER: there is no way to drag one without
 * disturbing the other, and lightening a colour without desaturating it is most of what
 * shading is. Four separate bars give that back by spending a strip of the window on each
 * channel. Holding SHIFT gives the same thing for nothing - and SHIFT ALREADY MEANS
 * CONSTRAIN here, twice over: it snaps a line to the pixel-art slopes and a corner grip to
 * the eight pixel grid. A third use of an idiom is cheaper to learn than a fourth control.
 *
 * The axis is decided by which way the hand has travelled furthest from the press, and kept
 * for the rest of the drag - so it does not flip about while a slow hand wanders.
 */
static float press_x = 0.0f, press_y = 0.0f;
static int   axis = 0;   /* 0 free, 1 saturation only, 2 value only */

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

	if (e->type == SDL_EVENT_MOUSE_BUTTON_UP) { grab = GRAB_NONE; axis = 0; return false; }

	if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		/* Pressing anywhere else finishes what was being typed, which is what a person means
		 * by it - losing it because the mouse moved would be the surprising reading. */
		if (editing && !in_rect(l.hex, x, y)) field_stop(true);

		press_x = x;
		press_y = y;
		axis    = 0;

		if (in_rect(l.hex, x, y)) { if (!editing) field_start(); return false; }

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
	case GRAB_SV: {
		if (keys_mods() & SDL_KMOD_SHIFT) {
			if (!axis) {
				float dx = SDL_fabsf(x - press_x), dy = SDL_fabsf(y - press_y);
				if (dx > 2.0f || dy > 2.0f) axis = dx >= dy ? 1 : 2;
			}
		} else {
			axis = 0;
		}

		if (axis != 2) s = clamp01((x - l.sv.x) / l.sv.w);
		if (axis != 1) v = 1.0f - clamp01((y - l.sv.y) / l.sv.h);
		break;
	}
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

		/* ONE BOX THAT ANSWERS IN BOTH DIRECTIONS: it says what the colour is, and typing
		 * over it says what the colour should be. A label with an input under it would be the
		 * same question asked twice. */
		SDL_SetRenderDrawColor(vng_ren, 0x0C, 0x0C, 0x0C, 0xFF);
		SDL_RenderFillRect(vng_ren, &l.hex);
		SDL_SetRenderDrawColor(vng_ren, editing ? 0xC0 : 0x38, editing ? 0xC0 : 0x38,
		                                editing ? 0xC0 : 0x38, 0xFF);
		SDL_RenderRect(vng_ren, &l.hex);

		if (editing) SDL_strlcpy(hex, field, sizeof hex);
		else         tool_hex(tool_colour(slot), hex, sizeof hex);

		text_print(vng_text, l.hex.x + 4.0f, l.hex.y, 0xDCDCDCFF, "#%s", hex);

		if (editing) {
			char  lead[16];
			float tw;
			SDL_snprintf(lead, sizeof lead, "#%s", hex);
			text_measure(vng_text, lead, &tw, NULL);

			SDL_FRect caret = { l.hex.x + 4.0f + tw + 1.0f, l.hex.y + 2.0f,
			                    1.0f, l.hex.h - 4.0f };
			SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xE0);
			SDL_RenderFillRect(vng_ren, &caret);
		}
	}

	for (int i = 0; i < SLOTS; i++) {
		SDL_FRect r = { l.tiles.x + (i % 8) * SWATCH, l.tiles.y + (i / 8) * SWATCH,
		                SWATCH - 1.0f, SWATCH - 1.0f };
		chip(r, swatch[i]);
	}
}

void colour_toggle (void)
{
	if (win) {
		bool on = !win_visible(win);
		if (!on) field_stop(true);   /* a window put away must not still hold the keyboard */
		win_show(win, on);
		return;
	}

	SDL_FRect  a = { 24.0f, 48.0f, OPEN_W, OPEN_H };
	SDL_FPoint m = { MIN_W, MIN_H };

	win = win_open("colour", a, m, body, on_event, NULL);
}

bool colour_visible (void) { return win_visible(win); }
