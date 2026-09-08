#include "colour.h"
#include "win.h"
#include "tool.h"
#include "core.h"
#include "keys.h"
#include "glyph.h"

#define OPEN_W  300.0f
#define OPEN_H  212.0f
#define MIN_W   220.0f
#define MIN_H   160.0f

#define BAR_W    14.0f
#define BARS      4       /* hue, saturation, value, alpha */

/*
 * THE WHEEL'S PROPORTIONS ARE THE FIRST VANGOPIX'S, kept because they are the design.
 *
 * Its __draw_color_wheel__ took an inner radius of 50 and a thickness of 20, so the ring runs
 * from 50 to 70 - the hole is five sevenths of the whole. The preview disc inside it had
 * radius 30 and the hue marker sat at 40, between the disc and the ring; the marker that
 * follows the hand sat at 85, outside. Written as fractions of the outer radius so the wheel
 * scales with the window instead of being a fixed 140 pixels for ever.
 */
#define RING_IN   (50.0f / 70.0f)
#define DISC      (30.0f / 70.0f)
#define MARK_IN   (40.0f / 70.0f)
#define MARK_OUT  (85.0f / 70.0f)
#define GAP       6.0f
#define SWATCH   16.0f
#define SLOTS     16      /* the "eight to sixteen", said back */

/* The sliders are generated small and stretched: a one-channel ramp is smooth by definition,
 * so there is nothing for more pixels to say. */
#define RAMP  64

/* The wheel is not, because a ring has EDGES, and an edge is exactly what a stretched
 * gradient cannot fake. Generated once at a size that is already bigger than it is drawn. */
#define WHEEL 192

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

/* The ring is hue at full saturation and value, so it never depends on the colour in hand and
 * is built exactly once. The first Vangopix rebuilt it into a cache; there is nothing to
 * rebuild. */
static SDL_Texture *tex_wheel = NULL;

static SDL_Texture *tex_bar[BARS] = { 0 };   /* rebuilt when the colour moves */
static Uint32 bars_of = 1u;

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

static float clamp01 (float f) { return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); }

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

/*
 * THE RING, AND ITS RIMS ARE SOFTENED.
 *
 * The first Vangopix tested `dist >= in_r && dist <= out_r + in_r` and painted or did not,
 * which leaves both rims as a staircase - the one place a wheel looks homemade. Coverage over
 * the last texel costs one subtraction and takes the staircase out, and it is why this is
 * generated at 192 and drawn smaller rather than generated small and stretched: a stretched
 * gradient can fake a gradient but not an EDGE.
 */
static void wheel_build (void)
{
	if (tex_wheel) return;

	Uint32 *px = (Uint32 *) SDL_malloc((size_t)WHEEL * WHEEL * sizeof(Uint32));
	if (!px) return;

	const float mid  = (WHEEL - 1) * 0.5f;
	const float soft = 1.0f / mid;          /* one texel, in fractions of the radius */

	for (int y = 0; y < WHEEL; y++) {
		for (int x = 0; x < WHEEL; x++) {
			float dx = ((float)x - mid) / mid;
			float dy = ((float)y - mid) / mid;
			float d  = SDL_sqrtf(dx * dx + dy * dy);

			/* How much of this texel is inside the ring: 1 well within, 0 well outside,
			 * and the slope between covers exactly the rim. */
			float in  = clamp01((d - (RING_IN - soft)) / (soft * 2.0f));
			float out = clamp01(((1.0f - soft) + soft * 2.0f - d) / (soft * 2.0f));
			float cov = in < out ? in : out;

			if (cov <= 0.0f) { px[y * WHEEL + x] = 0u; continue; }

			/* Negated y, so the hue runs anticlockwise from red at the right - which is how
			 * a colour wheel is drawn everywhere, and how the original had it. */
			float deg = SDL_atan2f(-dy, dx) * (180.0f / SDL_PI_F);
			if (deg < 0.0f) deg += 360.0f;

			px[y * WHEEL + x] = (hsv_argb(deg, 1.0f, 1.0f, 0xFF) & 0x00FFFFFFu)
			                  | ((Uint32)(cov * 255.0f + 0.5f) << 24);
		}
	}

	tex_wheel = ramp_make(WHEEL, WHEEL, px);
	SDL_free(px);
}

/*
 * FOUR SLIDERS, ONE PER CHANNEL, and each shows its own channel across its whole range WITH
 * THE OTHERS HELD - so what is under the pointer is what would be got. That is the first
 * Vangopix's __draw_slide_color__ and it is the only version that tells the truth.
 *
 * They are not redundant with the wheel: the wheel is hue, and saturation, value and alpha
 * have nowhere else to go.
 */
static void build (void)
{
	static Uint32 px[RAMP];

	wheel_build();

	Uint32 now = hsv_argb(h, s, v, alpha);
	if (bars_of == now) return;
	bars_of = now;

	for (int b = 0; b < BARS; b++) {
		for (int y = 0; y < RAMP; y++) {
			/* The top of a slider is the most of it, which is what a person expects of a
			 * vertical control and is the way round the original had. */
			float f = 1.0f - (float)y / (RAMP - 1);

			switch (b) {
			case 0: px[y] = hsv_argb(f * 359.99f, s, v, 0xFF);            break;
			case 1: px[y] = hsv_argb(h, f, v, 0xFF);                      break;
			case 2: px[y] = hsv_argb(h, s, f, 0xFF);                      break;
			default: px[y] = (hsv_argb(h, s, v, 0xFF) & 0x00FFFFFFu)
			               | ((Uint32)(f * 255.0f + 0.5f) << 24);         break;
			}
		}
		if (tex_bar[b]) SDL_DestroyTexture(tex_bar[b]);
		tex_bar[b] = ramp_make(1, RAMP, px);
	}
}

void colour_free (void)
{
	if (tex_wheel) SDL_DestroyTexture(tex_wheel);
	tex_wheel = NULL;

	for (int b = 0; b < BARS; b++) {
		if (tex_bar[b]) SDL_DestroyTexture(tex_bar[b]);
		tex_bar[b] = NULL;
	}
	bars_of = 1u;
}

/* ------------------------------------------------------------------------ the layout */

/* Everything is measured from the interior, so stretching the window stretches the picker
 * rather than leaving it in a corner. */
typedef struct {
	SDL_FRect  wheel;    /* the square the ring is drawn in */
	SDL_FPoint centre;
	float      radius;   /* the ring's outer radius */
	SDL_FRect  bar[BARS], slots, hex, tiles;
} LAYOUT;

static LAYOUT layout (SDL_FRect r)
{
	LAYOUT l;

	float rows  = SDL_ceilf(SLOTS / 8.0f);
	float bottom = SWATCH * rows + GAP + (vng_text ? text_line_height(vng_text) : 15.0f);

	float top_h = r.h - bottom - GAP;
	if (top_h < 24.0f) top_h = 24.0f;

	float bars_w = (BAR_W + GAP) * BARS;

	/* The wheel is SQUARE and centred in what is left of the row, because a ring squeezed into
	 * an oblong is an ellipse, and an ellipse says the hues are not evenly spaced. */
	float box = r.w - bars_w;
	if (box > top_h) box = top_h;
	if (box < 8.0f)  box = 8.0f;

	l.wheel  = (SDL_FRect){ r.x + (r.w - bars_w - box) * 0.5f,
	                        r.y + (top_h - box) * 0.5f, box, box };
	l.centre = (SDL_FPoint){ l.wheel.x + box * 0.5f, l.wheel.y + box * 0.5f };
	l.radius = box * 0.5f;

	for (int b = 0; b < BARS; b++)
		l.bar[b] = (SDL_FRect){ r.x + r.w - bars_w + GAP * 0.5f + b * (BAR_W + GAP),
		                        r.y, BAR_W, top_h };

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
static enum { GRAB_NONE, GRAB_WHEEL, GRAB_BAR } grab = GRAB_NONE;
static int grab_bar = 0;

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
		/* Pressing anywhere else finishes what was being typed, which is what a person means
		 * by it - losing it because the mouse moved would be the surprising reading. */
		if (editing && !in_rect(l.hex, x, y)) field_stop(true);

		if (in_rect(l.hex, x, y)) { if (!editing) field_start(); return false; }

		/* ANYWHERE INSIDE THE WHEEL'S REACH TAKES THE HUE, not only the ring itself. Aiming
		 * at a twenty pixel band is a worse gesture than pointing at a direction, and a
		 * direction is all the ring is asking for. The original did the same. */
		float dx = x - l.centre.x, dy = y - l.centre.y;
		if (SDL_sqrtf(dx * dx + dy * dy) <= l.radius) grab = GRAB_WHEEL;
		else {
			for (int b = 0; b < BARS; b++)
				if (in_rect(l.bar[b], x, y)) { grab = GRAB_BAR; grab_bar = b; }
		}

		if (grab == GRAB_NONE) {
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
	case GRAB_WHEEL: {
		/* The angle from the CENTRE to the hand, which is the readable version. The original
		 * measured it the other way round and then cancelled the difference with a minus
		 * sign in front of every cosine that used it. */
		float dx = x - l.centre.x, dy = y - l.centre.y;
		if (dx == 0.0f && dy == 0.0f) break;

		float deg = SDL_atan2f(-dy, dx) * (180.0f / SDL_PI_F);
		h = deg < 0.0f ? deg + 360.0f : deg;
		break;
	}
	case GRAB_BAR: {
		float f = 1.0f - clamp01((y - l.bar[grab_bar].y) / l.bar[grab_bar].h);

		switch (grab_bar) {
		case 0: h = f * 359.99f;                      break;
		case 1: s = f;                                break;
		case 2: v = f;                                break;
		default: alpha = (Uint8)(f * 255.0f + 0.5f);  break;
		}
		break;
	}
	default: break;
	}

	push();
	return true;   /* the drag is ours until the button comes up */
}

/* ------------------------------------------------------------------------- the pixels */

/*
 * A filled disc of one colour, laid over the same two tones every swatch in this program uses
 * so that a transparent colour reads as transparent here too.
 *
 * Row by row, because SDL draws no circles - and row by row is exactly what makes the two
 * tones easy: each row is split at the centre, which is the same left-half-lighter figure the
 * bars at the bottom of the screen show.
 */
static void disc (float cx, float cy, float r, Uint32 c)
{
	if (r < 1.0f) return;

	int ri = (int)r;

	for (int dy = -ri; dy <= ri; dy++) {
		float half = SDL_sqrtf((float)(ri * ri - dy * dy));
		float y    = cy + (float)dy;

		SDL_SetRenderDrawColor(vng_ren, 0x25, 0x25, 0x25, 0xFF);
		SDL_RenderLine(vng_ren, cx, y, cx + half, y);
		SDL_SetRenderDrawColor(vng_ren, 0x33, 0x33, 0x33, 0xFF);
		SDL_RenderLine(vng_ren, cx - half, y, cx, y);

		SDL_SetRenderDrawColor(vng_ren, (Uint8)((c >> 16) & 0xFF), (Uint8)((c >> 8) & 0xFF),
		                                (Uint8)(c & 0xFF), (Uint8)((c >> 24) & 0xFF));
		SDL_RenderLine(vng_ren, cx - half, y, cx + half, y);
	}

	/* A rim, so a pale colour still has an edge against the ring's hole. */
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xB0);
	for (int i = 0; i < 64; i++) {
		float a0 = (float)i * (2.0f * SDL_PI_F / 64);
		float a1 = (float)(i + 1) * (2.0f * SDL_PI_F / 64);
		SDL_RenderLine(vng_ren, cx + SDL_cosf(a0) * r, cy + SDL_sinf(a0) * r,
		                        cx + SDL_cosf(a1) * r, cy + SDL_sinf(a1) * r);
	}
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

	if (tex_wheel) SDL_RenderTexture(vng_ren, tex_wheel, NULL, &l.wheel);

	/*
	 * THE HOLE IN THE RING HOLDS THE RESULT, which is the best idea in the original's design:
	 * the colour being chosen sits in the middle of the thing choosing it, so the eye never
	 * has to travel to find out what the wheel just did.
	 */
	disc(l.centre.x, l.centre.y, l.radius * DISC, hsv_argb(h, s, v, alpha));

	/*
	 * TWO POINTERS, and the pair is the point. The inner one sits at the hue in hand, so the
	 * wheel always says where you ARE; the outer one follows the pointer while it is over the
	 * wheel, so it also says where you would GO. One arrow could only do one of those.
	 */
	{
		float rad = h * (SDL_PI_F / 180.0f);
		float mx  = l.centre.x + SDL_cosf(rad) * l.radius * MARK_IN;
		float my  = l.centre.y - SDL_sinf(rad) * l.radius * MARK_IN;
		glyph_draw(GLYPH_POINTER, mx, my, h + 90.0f, l.radius / 70.0f, 0xFFFFFFFF);
	}

	float px, py;
	SDL_GetMouseState(&px, &py);

	float dx = px - l.centre.x, dy = py - l.centre.y;
	if (SDL_sqrtf(dx * dx + dy * dy) <= l.radius && (dx != 0.0f || dy != 0.0f)) {
		float deg = SDL_atan2f(-dy, dx) * (180.0f / SDL_PI_F);
		if (deg < 0.0f) deg += 360.0f;

		float rad = deg * (SDL_PI_F / 180.0f);
		glyph_draw(GLYPH_POINTER,
		           l.centre.x + SDL_cosf(rad) * l.radius * MARK_OUT,
		           l.centre.y - SDL_sinf(rad) * l.radius * MARK_OUT,
		           deg - 90.0f, l.radius / 70.0f * 1.5f, 0xFFFFFFFF);
	}

	/* The alpha slider is the one with something behind it to show through. */
	for (int b = 0; b < BARS; b++) {
		if (b == BARS - 1) vangopix_desk_rect(l.bar[b]);
		if (tex_bar[b]) SDL_RenderTexture(vng_ren, tex_bar[b], NULL, &l.bar[b]);
	}

	const float level[BARS] = { h / 360.0f, s, v, alpha / 255.0f };
	for (int b = 0; b < BARS; b++)
		glyph_draw(GLYPH_POINTER, l.bar[b].x + BAR_W * 0.5f,
		           l.bar[b].y + (1.0f - level[b]) * l.bar[b].h,
		           90.0f, 0.8f, 0xFFFFFFFF);

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

/*
 * IT COMES UP UNDER THE POINTER, centred on it - the first Vangopix's behaviour, and the same
 * reason as the 1:1 panel: a window summoned to the hand needs no dragging to be where it is
 * wanted.
 */
void colour_toggle (void)
{
	float mx, my;
	SDL_GetMouseState(&mx, &my);

	if (win) {
		bool on = !win_visible(win);
		if (!on) field_stop(true);   /* a window put away must not still hold the keyboard */
		else     win_place(win, mx, my);
		win_show(win, on);
		return;
	}

	SDL_FRect  a = { 0.0f, 0.0f, OPEN_W, OPEN_H };
	SDL_FPoint m = { MIN_W, MIN_H };

	win = win_open("colour", a, m, body, on_event, NULL);
	win_place(win, mx, my);
}

bool colour_visible (void) { return win_visible(win); }
