#include "colour.h"
#include "ui.h"
#include "win.h"
#include "tool.h"
#include "core.h"
#include "keys.h"
#include "field.h"
#include "glyph.h"

/*
 * THE NUMBERS ARE THE FIRST VANGOPIX'S, taken from where it built this window.
 *
 *   tool_core.c          the window is 280 x 250 and its head is 18, so the inside is 280x232
 *   __palette_wheel__    the ring is drawn at (5, 20) with in_r 50 and out_r 20, so the wheel
 *                        is 140 across and its centre is at (75, 90)
 *   __palette_slides__   four sliders, 16 x 150, at x = 160, 190, 220, 250 and y = 32
 *   __palette_input__    the hex box is {a.x + 5, a.y + 150, 128, 20}
 *
 * Written below in the INTERIOR's coordinates, which is 18 less in y than the window's.
 *
 * THE ONE NUMBER NOT COPIED IS THE HEIGHT. The original left sixty pixels of clear band under
 * the hex box because its palette had an ADD button down there; this window has no such thing,
 * and sixty pixels of nothing reads as a mistake rather than as room. The band is trimmed to
 * FOOT, which is what keeps the stretch corner clear of everything - the whole of what it is
 * for now.
 */
#define OPEN_W  320.0f
#define OPEN_H  220.0f
#define MIN_W   250.0f
#define MIN_H   150.0f

#define TOP      14.0f    /* the sliders' top edge: 32 in the window, less the 18 head */
#define LEFT      5.0f
#define RIGHT    14.0f    /* 280 - (250 + 16), what the original left past the last slider */
#define BAR_W    20.0f
#define BAR_STEP 30.0f    /* 190 - 160 */
#define HEX_H    ui_row()
#define FOOT     (ui_grip() + ui_pad())   /* the band the stretch corner lives in */

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

/*
 * BOTH MARKERS TOUCH THE RING, FROM OPPOSITE SIDES, so the pair reads as two fingers pressing
 * the hue between them - which is what the first Vangopix's two arrows look like.
 *
 * The glyph's tip is 8 units above its origin in its own space, so an arrow whose tip is to
 * graze a rim has its origin one tip's length back from it. The inner one sits in the hole
 * with its tip on the ring's inner edge pointing OUT; the outer one sits past the ring with
 * its tip on the outer edge pointing IN, and its tail hangs outside the window - which is the
 * style, not an accident, and is why win_unclip exists.
 */
#define MARK_TIP   8.0f

/*
 * THE HAIRLINE THE INNER MARKER KEEPS OFF THE RING, in pixels.
 *
 * Its tip landing ON the inner edge made the arrow and the ring one shape: the white tip and
 * the coloured band met with nothing between them, so at a glance the marker read as part of
 * the wheel rather than as a thing pointing at it. A pixel and a half of hole showing through
 * is all it takes to separate them - a hair over one because the tip is a POINT rather than a
 * flat end, so a single pixel of clearance at the very tip is already less than that a step
 * back along either edge of it.
 *
 * NOT scaled with the wheel, unlike everything else about the markers. This is not a
 * proportion - a gap is either visible or it is not, and one that grew to four pixels on a
 * stretched window would stop being a hairline and start being a distance.
 */
#define MARK_GAP   1.5f
#define SCALE_IN   1.0f
#define SCALE_OUT  1.5f    /* the original's, and it wants to be the bolder of the two */

/*
 * THE RADIUS THOSE TWO SCALES WERE CHOSEN AT, and everything is measured against it so the
 * arrows GROW WITH THE WHEEL.
 *
 * A marker that keeps its own size while the ring it presses on gets bigger stops being a
 * marker: at a stretched window it reads as a speck stuck to the rim, and at a small one it
 * swallows the ring. The whole geometry already follows the radius - the hole, the disc, where
 * each tip lands - so the arrows have to as well, or they are the one thing in the wheel that
 * does not agree with the rest of it.
 *
 * The slider markers are deliberately NOT scaled: a slider keeps its 16 pixel width however
 * the window is pulled, and only grows taller, so an arrow that grew with the window would
 * hang off the sides of the one thing it sits on.
 */
#define MARK_REF  70.0f

/*
 * WHERE THE OUTER MARKER'S TIP LANDS, as a fraction of the radius. THIS IS THE ONE KNOB for
 * moving that arrow in or out: 1.0 puts its tip exactly on the ring's outer rim, less pulls
 * it in over the ring, more pushes it out past the window.
 *
 * The original put it at 85 of 70 - a fifth of a radius OUTSIDE the ring - and with a centre
 * at (75, 90) that reached x = -10, off the left edge of its own window. It drew there anyway,
 * over whatever was behind, because nothing clipped it; here that is asked for by name, with
 * win_unclip, rather than being what happens by default.
 *
 * It sits on the rim because the arrow then TOUCHES the hue it points at instead of floating
 * a gap away from it - and because the pair only reads as two fingers pressing the hue between
 * them while both are actually on the ring.
 */
#define MARK_OUT  1.0f

/* Half a pointer glyph and its shadow. The wheel is held this far in from the space it is
 * given, so the marker at the rim has room for its own body. */
#define MARK_PAD  10.0f
#define GAP       6.0f

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
static float h = 0.0f, s = 1.0f, v = 0.0f;
static Uint8 alpha = 0xFF;

/*
 * WHICH OF THE TWO IS BEING FILLED IS SAID BY THE MOUSE BUTTON, not by a control.
 *
 * That is the first Vangopix's own arrangement - `if (mouse_left) color_front = color;
 * if (mouse_right) color_back = color;` in its __palette_getcolor__ - and it is the rule the
 * whole of this program already runs on: the button that takes a colour is the button that
 * lays it down. Two chips inside the window to choose between them would have been a third
 * way of saying something the mouse already says, next to a readout at the bottom of the
 * screen that already shows both.
 */
/* v starts at 0 because START_1 is BLACK, and the two have to agree from the first frame: the
 * picker only re-reads the slot when it finds it CHANGED, so a disc that opened saying red
 * over a slot holding black would go on saying it until something else moved. */
static int slot = 0;
static Uint32 last = 0xFF000000u; /* what this window last wrote, to notice outside changes */

/*
 * THE HEX READOUT IS THE HEX FIELD.
 *
 * A published palette arrives as a string - #2E3440, and fifteen more like it - and with
 * nowhere to put one the only way in is to paste the image and eyedrop it. The first Vangopix
 * had a text box for exactly this and was right to.
 *
 * It is not a second control beside the readout, though: the readout already says what the
 * colour is, and typing over it says what it should be. ONE BOX THAT ANSWERS IN BOTH
 * DIRECTIONS beats a label with an input under it - which is what field_draw's `show` is for.
 *
 * The box itself is field.c now. It was written here first, and it is lifted out because the
 * animation clip editor wants seven of them; that this one still behaves is the proof it
 * generalised.
 */
static VNG_FIELD *hex_box = NULL;

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
	field_free(hex_box);
	hex_box = NULL;

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
	SDL_FRect  bar[BARS], hex;
} LAYOUT;

static LAYOUT layout (SDL_FRect r)
{
	LAYOUT l;

	/* The sliders keep their width and spacing and are held against the RIGHT edge, so at the
	 * size this opens at they land on 160, 190, 220 and 250 exactly. Stretching the window
	 * makes them TALLER - a taller slider is a finer one - rather than fatter. */
	float block  = BAR_STEP * (BARS - 1) + BAR_W;
	float bars_x = r.w - RIGHT - block;
	if (bars_x < LEFT * 2.0f) bars_x = LEFT * 2.0f;

	float bars_h = r.h - TOP - FOOT;
	if (bars_h < 8.0f) bars_h = 8.0f;

	for (int b = 0; b < BARS; b++)
		l.bar[b] = (SDL_FRect){ r.x + bars_x + b * BAR_STEP, r.y + TOP, BAR_W, bars_h };

	/* The hex box goes UNDER THE WHEEL and clear of the sliders, which is where the original
	 * put it: the two columns share the height of the window without sharing its width. It
	 * spans the left column rather than a fixed 128, so a wider window gives it more room
	 * instead of leaving a gap nobody asked for. */
	l.hex = (SDL_FRect){ r.x + LEFT, r.y + r.h - FOOT - HEX_H,
	                     bars_x - LEFT * 2.0f, HEX_H };

	/* The wheel is SQUARE, because a ring squeezed into an oblong is an ellipse and an ellipse
	 * says the hues are not evenly spaced. It fills what is left above the hex box, LESS the
	 * pad the rim marker needs for its own body - a marker cut in half by the window's edge is
	 * what happens otherwise, since win.c clips the interior. */
	float box = bars_x - LEFT * 2.0f - MARK_PAD * 2.0f;
	float lid = l.hex.y - r.y - GAP - MARK_PAD * 2.0f;
	if (box > lid)  box = lid;
	if (box < 8.0f) box = 8.0f;

	l.wheel  = (SDL_FRect){ r.x + (bars_x - box) * 0.5f, r.y + MARK_PAD, box, box };
	l.centre = (SDL_FPoint){ l.wheel.x + box * 0.5f, l.wheel.y + box * 0.5f };
	l.radius = box * 0.5f;

	return l;
}

static bool in_rect (SDL_FRect r, float x, float y)
{
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}



/* ------------------------------------------------------------------------- the field */

/* What ENTER means. ESC and a press elsewhere close the box without arriving here, so
   escaping costs nothing and a mis-click cannot commit half a colour. */
static void hex_done (const char *text, void *ctx)
{
	(void)ctx;

	Uint32 c;
	if (!tool_hex_read(text, &c)) return;

	argb_hsv(c, &h, &s, &v);
	alpha = (Uint8)((c >> 24) & 0xFF);
	last  = c;
	tool_set_colour(slot, c);
}

static void hex_start (void)
{
	char now[16];

	if (!hex_box) return;

	tool_hex(tool_colour(slot), now, sizeof now);
	field_open(hex_box, now);
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
		if (!in_rect(l.hex, x, y)) field_close(hex_box, false);

		/* The button decides which colour this press is filling, before anything is read. */
		slot = (e->button.button == SDL_BUTTON_RIGHT) ? 1 : 0;

		/* Taken, so the window does not read it as somewhere to be dragged from. */
		if (in_rect(l.hex, x, y)) { hex_start(); return true; }

		/* ANYWHERE INSIDE THE WHEEL'S REACH TAKES THE HUE, not only the ring itself. Aiming
		 * at a twenty pixel band is a worse gesture than pointing at a direction, and a
		 * direction is all the ring is asking for. The original did the same. */
		float dx = x - l.centre.x, dy = y - l.centre.y;
		if (SDL_sqrtf(dx * dx + dy * dy) <= l.radius) grab = GRAB_WHEEL;
		else {
			for (int b = 0; b < BARS; b++)
				if (in_rect(l.bar[b], x, y)) { grab = GRAB_BAR; grab_bar = b; }
		}

		/* Nothing under the press that this window wants. */
		if (grab == GRAB_NONE) return false;
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

		/*
		 * A HUE MEANS NOTHING AT VALUE ZERO - every one of them is black - so a wheel used on
		 * a black colour would turn and turn and change nothing. Reaching for the ring is
		 * asking for that hue, so the two channels that can swallow it are lifted out of the
		 * way. The sliders are still there for anyone who wants to put them back.
		 */
		if (v <= 0.0f) v = 1.0f;
		if (s <= 0.0f) s = 1.0f;
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
	vangopix_desk_disc(l.centre.x, l.centre.y, l.radius * DISC,
	                   hsv_argb(h, s, v, alpha), 0xB0000000u);

	/*
	 * TWO POINTERS, and the pair is the point. The inner one sits at the hue in hand, so the
	 * wheel always says where you ARE; the outer one follows the pointer while it is over the
	 * wheel, so it also says where you would GO. One arrow could only do one of those.
	 */
	{
		/* Inside the hole, tip on the ring's INNER edge, pointing out. */
		float sc  = SCALE_IN * (l.radius / MARK_REF);
		float rad = h * (SDL_PI_F / 180.0f);
		float at  = l.radius * RING_IN - MARK_TIP * sc - MARK_GAP;

		glyph_draw(GLYPH_POINTER,
		           l.centre.x + SDL_cosf(rad) * at,
		           l.centre.y - SDL_sinf(rad) * at,
		           h - 90.0f, sc, 0xFFFFFFFF);
	}

	float px, py;
	SDL_GetMouseState(&px, &py);

	float dx = px - l.centre.x, dy = py - l.centre.y;
	if (SDL_sqrtf(dx * dx + dy * dy) <= l.radius && (dx != 0.0f || dy != 0.0f)) {
		float deg = SDL_atan2f(-dy, dx) * (180.0f / SDL_PI_F);
		if (deg < 0.0f) deg += 360.0f;

		/* Past the ring, tip on the OUTER edge, pointing in - so the two arrows meet the same
		 * band of colour from either side.
		 *
		 * THE SCISSORS COME OFF FOR THIS ONE CALL. Its tail stands outside the wheel and,
		 * near the top of a small window, outside the window itself; cut short it stops
		 * reading as a thing pressing on the ring. Every other line here stays clipped. */
		float sc  = SCALE_OUT * (l.radius / MARK_REF);
		float rad = deg * (SDL_PI_F / 180.0f);
		float at  = l.radius * MARK_OUT + MARK_TIP * sc;

		win_unclip(win);
		glyph_draw(GLYPH_POINTER,
		           l.centre.x + SDL_cosf(rad) * at,
		           l.centre.y - SDL_sinf(rad) * at,
		           deg + 90.0f, sc, 0xFFFFFFFF);
		win_clip(win);
	}

	for (int b = 0; b < BARS; b++) {
		/* The alpha slider is the one with something behind it to show through. */
		if (b == BARS - 1) vangopix_desk_rect(l.bar[b]);
		if (tex_bar[b]) SDL_RenderTexture(vng_ren, tex_bar[b], NULL, &l.bar[b]);

		/* A WHITE FRAME AROUND EACH ONE, which the original drew and which earns its place: a
		 * slider is a band of colour on a dark window, and without an edge its ends are
		 * wherever the colour happens to go dark. The original's rect stood one pixel proud
		 * of the bar (r.w+1, r.h+1) and so does this. */
		SDL_FRect f = { l.bar[b].x - 1.0f, l.bar[b].y - 1.0f,
		                l.bar[b].w + 2.0f, l.bar[b].h + 2.0f };
		SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xFF);
		SDL_RenderRect(vng_ren, &f);
	}

	const float level[BARS] = { h / 360.0f, s, v, alpha / 255.0f };
	for (int b = 0; b < BARS; b++)
		glyph_draw(GLYPH_POINTER, l.bar[b].x + BAR_W * 0.5f,
		           l.bar[b].y + (1.0f - level[b]) * l.bar[b].h,
		           90.0f, 1.0f, 0xFFFFFFFF);

	if (vng_text) {
		char hex[16];
		tool_hex(tool_colour(slot), hex, sizeof hex);

		/* The box shows the SLOT while it is closed and what is being typed while it is open,
		 * which is the same box answering the question and taking the answer. */
		field_draw(hex_box, l.hex, NULL, hex);
	}
}

/*
 * IT COMES UP UNDER THE POINTER, centred on it - the first Vangopix's behaviour, and the same
 * reason as the 1:1 panel: a window summoned to the hand needs no dragging to be where it is
 * wanted.
 *
 * BUT ONLY WHEN IT IS BEING SUMMONED. A window already on screen has been PARKED somewhere on
 * purpose, and moving it under the hand every time something raises it would undo that. So
 * this places it when it comes up and only raises it when it is already there.
 */
void colour_open (void)
{
	if (win && win_visible(win)) {
		win_show(win, true);   /* showing raises, which is all an open can mean here */
		return;
	}

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	if (!win) {
		SDL_FRect  a = { 0.0f, 0.0f, OPEN_W, OPEN_H };
		SDL_FPoint m = { MIN_W, MIN_H };

		/* WITH THE WINDOW AND NOT WITH THE FIRST CLICK. Built lazily in the press handler,
		 * the hex readout did not exist until somebody clicked it - so the one thing it is
		 * there to say was invisible until you asked it to say something else. */
		if (!hex_box) hex_box = field_make(VNG_FIELD_HEX, hex_done, NULL);

		win = win_open("colour", a, m, body, on_event, NULL);
	}

	win_place(win, mx, my);
	win_show(win, true);
}

void colour_toggle (void)
{
	if (win && win_visible(win)) {
		field_close(hex_box, false);   /* a window put away must not hold the keyboard */
		win_show(win, false);
		return;
	}
	colour_open();
}

bool colour_visible (void) { return win_visible(win); }
