#include "tool.h"
#include "view.h"
#include "keys.h"
#include "glyph.h"
#include "tabbar.h"
#include "sidebar.h"

/*
 * The two colours a fresh program starts with. 0xAARRGGBB, matching the ARGB8888 the
 * document is.
 *
 * COLOUR 2 IS NOTHING, and that is what makes the right button rub out without an eraser
 * being involved. In a program that keeps alpha, rubbing out IS drawing with nothing - and
 * white would be a colour somebody chose, which is the whole reason the desk is a
 * checkerboard.
 */
#define START_1  0xFF000000u
#define START_2  0x00000000u

/* Below this the tip outline covers more of the sheet than it points at - at 1:1 a pixel is
 * one screen pixel and a box around it swallows its eight neighbours. Under it the crosshair
 * is the only signal, which is right: at that scale the image is being LOOKED at. A tip
 * bigger than one pixel is outlined at any zoom, because its SIZE is what has to be visible
 * before it is used. */
#define OUTLINE_ZOOM  2.0f

/* How far off the pointer the hex readout sits, and the padding inside its bar. */
#define READ_OFF_X  14.0f
#define READ_OFF_Y  16.0f
#define READ_PAD     4.0f

/*
 * WHERE THE TOOL GLYPH HANGS, AND THE NUMBERS ARE THE FIRST VANGOPIX'S OWN:
 * (mouse.x + 16, mouse.y - 16), from tool_show_icons in its src/tool/tool_misc.c.
 *
 * Up and to the right, because that is the quadrant a right-handed hand is not covering with
 * the mouse itself, and sixteen because the glyphs are drawn on a grid of about -12..+10 -
 * any less and the outline would reach back over the pixel it is reporting on.
 */
#define GLYPH_OFF_X   16.0f
#define GLYPH_OFF_Y  -16.0f
#define GLYPH_REACH   12.0f   /* how far a glyph extends from its origin, for the flip */

/* The loaded colours sit this far from the corner, with this much between them. */
#define SLOT_MARGIN  8.0f
#define SLOT_GAP     6.0f

/* The largest tip. Past this a stroke is not a brush any more, and the outline stops meaning
 * anything on screen. */
#define TIP_MAX  256

static SDL_Cursor *cur_cross = NULL;
static SDL_Cursor *cur_arrow = NULL;
static SDL_Cursor *cur_now   = NULL;   /* what is on screen, so the OS is not asked twice */

/* The two loaded colours SURVIVE a stroke - they are the tool's, not the drag's. `laying` is
 * what the current drag puts down, copied at the press so that picking mid-stroke could
 * never change the colour of a line already begun. */
static Uint32 colour[2] = { START_1, START_2 };
static Uint32 laying    = START_1;

static TOOL current = T_PENCIL;

/* Sizes and steps, both the first Vangopix's: vng_tool.tool_size in its tool_core.c, and the
 * scale_step it sets per case in the same switch. */
static int size[T_LOT] = { 1, 1, 1, 1, 20, 1, 20, 1 };

static const int step[T_LOT] = { 1, 1, 1, 1,  5, 1,  3, 3 };

/*
 * The change-colours limiter: 0 the whole sheet, 1 a circle, 2 a square. SHIFT+TAB swaps
 * between the circle and the square; at size 1 it is the whole sheet and there is no shape
 * to choose. The original used bare TAB, which is the project sidebar's here.
 */
static int limiter = 0;

static bool   drawing = false;
static Uint8  button  = 0;                  /* the one that started the stroke */
static int    last_x = 0, last_y = 0;       /* where the previous sample landed */
static int    anchor_x = 0, anchor_y = 0;   /* where a shape was begun */

/* True while a CTRL press is being dragged across the sheet, absorbing as it goes. */
static bool   picking = false;
static int    pick_slot = 0;

/* Which slot a button owns. The button that takes a colour is the button that lays it down,
 * so there is nothing to remember about where a pick landed. */
static int slot_of (Uint8 btn) { return btn == SDL_BUTTON_RIGHT ? 1 : 0; }

/* A shape is anchored: press, drag, release, with the preview redrawn from the anchor on
 * every motion rather than accumulated. */
static bool anchored (TOOL t) { return t == T_LINE || t == T_RECT || t == T_ELLIPSE; }

/* Does its whole job on the press and has nothing to add on the way out. The bucket is one
 * too, but it goes through tool_fill and never reaches the generic path. */
static bool instant (TOOL t) { return t == T_CHANGE; }

TOOL tool_current  (void) { return current; }
int  tool_tip_size (void) { return size[current]; }

Uint32 tool_colour (int slot) { return colour[slot == 1 ? 1 : 0]; }

void tool_pick (VNG_TAB *t, int x, int y, int slot)
{
	if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return;

	/* From the document and never from the preview: what is absorbed is a colour that is IN
	 * the drawing, not one still on its way in. */
	colour[slot == 1 ? 1 : 0] = t->pixels[(size_t)y * t->w + x];
}

void tool_hex (Uint32 argb, char *dst, size_t cap)
{
	if (!dst || cap == 0) return;

	SDL_snprintf(dst, cap, "%02X%02X%02X%02X",
	             (unsigned)((argb >> 16) & 0xFF),   /* R */
	             (unsigned)((argb >>  8) & 0xFF),   /* G */
	             (unsigned)( argb        & 0xFF),   /* B */
	             (unsigned)((argb >> 24) & 0xFF));  /* A, last - see tool.h */
}

bool tool_init (void)
{
	cur_cross = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
	cur_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);

	if (!cur_cross || !cur_arrow) {
		SDL_Log("cursors: %s", SDL_GetError());
		return false;
	}
	return true;
}

void tool_free (void)
{
	/* The system is put back on its own cursor before either of ours is destroyed. */
	if (cur_arrow) SDL_SetCursor(cur_arrow);

	if (cur_cross) SDL_DestroyCursor(cur_cross);
	if (cur_arrow) SDL_DestroyCursor(cur_arrow);
	cur_cross = cur_arrow = cur_now = NULL;
}

/*
 * Screen point to document pixel.
 *
 * SDL_floorf and NOT a cast to int: a cast truncates toward zero, so a world coordinate of
 * -0.5 - the pointer just off the left edge - comes out as pixel 0 instead of -1, and a
 * stroke leaving the sheet on that side would smear along the first column instead of
 * leaving.
 */
static void pixel_of (VNG_TAB *t, float sx, float sy, int *px, int *py)
{
	SDL_FPoint w = view_screen_to_world(t, sx, sy);
	*px = (int) SDL_floorf(w.x);
	*py = (int) SDL_floorf(w.y);
}

static bool inside (VNG_TAB *t, int x, int y)
{
	return x >= 0 && y >= 0 && x < t->w && y < t->h;
}

static void put (VNG_TAB *t, int x, int y)
{
	/* The touched check is what the mask is FOR. With an opaque colour it changes nothing,
	 * but a half transparent tip passing over its own output inside one stroke would blend
	 * onto itself and saturate the transparency into opacity - the detail the first Vangopix
	 * documents at the top of its main.c. */
	if (!vng_tab_touched(t, x, y))
		vng_tab_put(t, x, y, laying);
}

/* --------------------------------------------------------------------------- the tip */

/*
 * THE SMALL TIPS ARE DRAWN BY HAND, NOT BY A CIRCLE FORMULA, and this is the piece of the
 * first Vangopix most easily lost in a rewrite (its tool_circle_fill).
 *
 * A mathematical circle of radius 2 or 3 comes out as a lopsided smudge - there are not
 * enough pixels in it for the formula to mean anything, and every pixel artist already knows
 * what those brushes are supposed to look like. So sizes 2 to 5 are five-by-five bitmaps
 * placed by eye, size 1 is exactly one pixel, and only from 6 up is a circle computed.
 */
static const Uint8 tip_small[4][25] = {
	{ 0,0,0,0,0,  0,1,1,0,0,  0,1,1,0,0,  0,0,0,0,0,  0,0,0,0,0 },   /* 2: a square   */
	{ 0,0,0,0,0,  0,0,1,0,0,  0,1,1,1,0,  0,0,1,0,0,  0,0,0,0,0 },   /* 3: a diamond  */
	{ 0,1,1,0,0,  1,1,1,1,0,  1,1,1,1,0,  0,1,1,0,0,  0,0,0,0,0 },   /* 4: a round 4  */
	{ 0,1,1,1,0,  1,1,1,1,1,  1,1,1,1,1,  1,1,1,1,1,  0,1,1,1,0 },   /* 5: a round 5  */
};

/* The round tip: pencil, line, rect, ellipse. */
static void tip_round (VNG_TAB *t, int cx, int cy, int r)
{
	if (r <= 1) { put(t, cx, cy); return; }

	if (r <= 5) {
		for (int y = 0; y < 5; y++)
			for (int x = 0; x < 5; x++)
				if (tip_small[r - 2][y * 5 + x])
					put(t, cx + x - 2, cy + y - 2);
		return;
	}

	for (int y = -r; y <= r; y++)
		for (int x = -r; x <= r; x++)
			if (x * x + y * y <= r * r)
				put(t, cx + x, cy + y);
}

/* The eraser is a SQUARE, and deliberately not the round tip: rubbing out is about clearing
 * an area, and a square is the shape whose edges a person can predict. Odd sizes only, so
 * there is a centre pixel to aim with. */
static void tip_square (VNG_TAB *t, int cx, int cy, int n)
{
	if (n < 1) n = 1;
	if ((n & 1) == 0) n++;

	int h = n / 2;
	for (int y = -h; y <= h; y++)
		for (int x = -h; x <= h; x++)
			put(t, cx + x, cy + y);
}

/* The tip of whatever tool is current. */
static void tip (VNG_TAB *t, int x, int y)
{
	if (current == T_ERASER) tip_square(t, x, y, size[current]);
	else                     tip_round (t, x, y, size[current]);
}

/* -------------------------------------------------------------------- the primitives */

/*
 * Bresenham between two samples, and it is not a refinement.
 *
 * A hand moving at any speed outruns the motion events: the pointer arrives eight pixels
 * from where it was, and a tool that stamps only where the events land draws a dotted line.
 * Every pixel editor joins the samples, and this is that join.
 */
static void plot_line (VNG_TAB *t, int x0, int y0, int x1, int y1)
{
	int dx =  SDL_abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -SDL_abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	for (;;) {
		/* No bounds test: a stroke that runs off the sheet and comes back is ONE stroke,
		 * and the line between two samples outside it still has to be walked. vng_tab_put
		 * clips, so the part that lands is the part that lands. */
		tip(t, x0, y0);

		if (x0 == x1 && y0 == y1) break;

		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

/*
 * SHIFT SNAPS A LINE TO THE PIXEL-ART SLOPES, and the ladder is the reason it exists:
 * horizontal, 2:1, 1:1, 1:2, vertical. The 2:1 is the isometric slope - a line that goes two
 * across for every one down is the one that comes out CLEAN on a pixel grid, with a run of
 * two identical steps all the way. An arbitrary angle produces runs of 3, 2, 3, 2, 2 and
 * reads as a wobble. The first Vangopix's table, in its tool_brush.c, is these twelve
 * sectors, and its own comment draws them as a grid of angles.
 *
 * WHAT IS PRESERVED IS THE VERTICAL TRAVEL, and X is recomputed from it - which is what the
 * original does and what the hand expects: the length of the line follows how far the hand
 * moved down the screen, and the slope decides the rest. Horizontal is the exception, where
 * Y is flattened onto the anchor instead.
 *
 * The boundaries here are CONTIGUOUS, which the original's were not: it used a chain of
 * `dir > a && dir < b`, so an angle landing exactly on 30 or 80 fell through every test and
 * the line stayed free. Never seen in practice, and there is no reason to reproduce it.
 */
typedef enum { SNAP_FLAT, SNAP_UPRIGHT, SNAP_SLOPE } SNAP_KIND;

static const struct { float upto; SNAP_KIND kind; float k; } iso[] = {
	{  10.0f, SNAP_FLAT,    0.0f },
	{  30.0f, SNAP_SLOPE,   2.0f },   /*  26 deg - two across, one up */
	{  50.0f, SNAP_SLOPE,   1.0f },   /*  45 */
	{  80.0f, SNAP_SLOPE,   0.5f },   /*  64 - one across, two up */
	{ 100.0f, SNAP_UPRIGHT, 0.0f },
	{ 130.0f, SNAP_SLOPE,  -0.5f },
	{ 150.0f, SNAP_SLOPE,  -1.0f },
	{ 170.0f, SNAP_SLOPE,  -2.0f },
	{ 190.0f, SNAP_FLAT,    0.0f },
	{ 210.0f, SNAP_SLOPE,   2.0f },
	{ 230.0f, SNAP_SLOPE,   1.0f },
	{ 260.0f, SNAP_SLOPE,   0.5f },
	{ 280.0f, SNAP_UPRIGHT, 0.0f },
	{ 310.0f, SNAP_SLOPE,  -0.5f },
	{ 330.0f, SNAP_SLOPE,  -1.0f },
	{ 350.0f, SNAP_SLOPE,  -2.0f },
	{ 360.1f, SNAP_FLAT,    0.0f },   /* the far side of 350, which wraps to the first */
};

void tool_snap_iso (int ax, int ay, int *x, int *y)
{
	/* Negated, so the angle grows anticlockwise the way a person reads one, on a screen whose
	 * Y grows downward. 90 is straight up. */
	float dir = -SDL_atan2f((float)(*y - ay), (float)(*x - ax)) * (180.0f / SDL_PI_F);
	if (dir < 0.0f) dir += 360.0f;

	for (size_t i = 0; i < SDL_arraysize(iso); i++) {
		if (dir >= iso[i].upto) continue;

		switch (iso[i].kind) {
		case SNAP_FLAT:    *y = ay; return;
		case SNAP_UPRIGHT: *x = ax; return;
		case SNAP_SLOPE:
			*x = ax + (int)((float)(ay - *y) * iso[i].k);
			return;
		}
	}
}

static void plot_rect (VNG_TAB *t, int x0, int y0, int x1, int y1)
{
	plot_line(t, x0, y0, x1, y0);
	plot_line(t, x1, y0, x1, y1);
	plot_line(t, x1, y1, x0, y1);
	plot_line(t, x0, y1, x0, y0);
}

/*
 * Midpoint ellipse, inscribed in the dragged rectangle rather than centred on the press.
 * Dragging a box and getting the ellipse that fits it is what every editor does, and it is
 * the only reading where both corners mean something.
 */
static void plot_ellipse (VNG_TAB *t, int x0, int y0, int x1, int y1)
{
	int left = x0 < x1 ? x0 : x1, right  = x0 < x1 ? x1 : x0;
	int top  = y0 < y1 ? y0 : y1, bottom = y0 < y1 ? y1 : y0;

	long a = (right - left) / 2, b = (bottom - top) / 2;
	long cx = left + a, cy = top + b;

	/* Degenerate in one axis is a line, and drawing it as one beats drawing nothing. */
	if (a == 0 || b == 0) { plot_line(t, x0, y0, x1, y1); return; }

	long a2 = a * a, b2 = b * b;
	long x = 0, y = b;
	long sigma = 2 * b2 + a2 * (1 - 2 * b);

	while (b2 * x <= a2 * y) {
		tip(t, (int)(cx + x), (int)(cy + y));
		tip(t, (int)(cx - x), (int)(cy + y));
		tip(t, (int)(cx + x), (int)(cy - y));
		tip(t, (int)(cx - x), (int)(cy - y));
		if (sigma >= 0) { sigma += 4 * a2 * (1 - y); y--; }
		sigma += b2 * (4 * x + 6);
		x++;
	}

	x = a; y = 0;
	sigma = 2 * a2 + b2 * (1 - 2 * a);

	while (a2 * y <= b2 * x) {
		tip(t, (int)(cx + x), (int)(cy + y));
		tip(t, (int)(cx - x), (int)(cy + y));
		tip(t, (int)(cx + x), (int)(cy - y));
		tip(t, (int)(cx - x), (int)(cy - y));
		if (sigma >= 0) { sigma += 4 * b2 * (1 - x); x--; }
		sigma += a2 * (4 * y + 6);
		y++;
	}
}

/*
 * Flood fill, by SCANLINE and not by recursion.
 *
 * The obvious four-way recursion runs the C stack out on a large fill - a 4000x4000 sheet
 * filled corner to corner is sixteen million frames deep. Filling whole spans and pushing
 * only the starts of the spans above and below keeps the stack proportional to the HEIGHT of
 * the region rather than to its area.
 *
 * THE MASK IS THE VISITED SET, and that is free: a pixel already marked in this stroke is a
 * pixel already filled, so nothing else has to remember where the fill has been.
 */
static void plot_flood (VNG_TAB *t, int sx, int sy, bool barrier)
{
	if (!inside(t, sx, sy)) return;

	Uint32 target = t->pixels[(size_t)sy * t->w + sx];

	/*
	 * TWO FILLS, AND THE DIFFERENCE IS THE MATCH TEST.
	 *
	 * The ordinary bucket spreads across ONE COLOUR and stops where that colour stops. The
	 * barrier spreads across EVERYTHING and stops only where it meets the colour it is
	 * laying down - which is the tool for painting inside an outline you have just drawn,
	 * whatever is in there. A bucket refuses to cross a region of mixed shades; the barrier
	 * does not care what it is covering, only where the wall is.
	 *
	 * That is the first Vangopix's tool_flood_fill_adv, which it calls with the new colour
	 * and the barrier colour set to the same value - and passing the same colour twice is
	 * exactly what makes it terminate: a painted pixel becomes a wall.
	 */
	if (!barrier && target == laying) return;   /* a no-op with a cost */

	/* Two ints per span start. One entry per row is the worst case, and a few rows of slack
	 * cost nothing beside the buffers a document already carries. */
	int  cap   = t->h * 4 + 64;
	int *stack = (int *) SDL_malloc((size_t)cap * 2 * sizeof(int));
	if (!stack) return;

	int top = 0;
	stack[0] = sx; stack[1] = sy; top = 1;

	while (top > 0) {
		top--;
		int x = stack[top * 2], y = stack[top * 2 + 1];

		if (!inside(t, x, y)) continue;

		/* The document is read, never the preview, so the wall a barrier fill stops at is
		 * the drawing as it was when the button went down. The mask guards against walking
		 * back over what this fill has already covered. */
		#define MATCH(rr, xx) 			(!t->mask[(rr) + (xx)] && (barrier ? t->pixels[(rr) + (xx)] != laying 			                                   : t->pixels[(rr) + (xx)] == target))

		size_t row = (size_t)y * t->w;
		if (!MATCH(row, x)) continue;

		int x0 = x;
		while (x0 > 0 && MATCH(row, x0 - 1)) x0--;

		int x1 = x;
		while (x1 < t->w - 1 && MATCH(row, x1 + 1)) x1++;

		for (int i = x0; i <= x1; i++)
			vng_tab_put(t, i, y, laying);

		/* One push per RUN on each neighbouring row, not one per pixel. */
		for (int dy = -1; dy <= 1; dy += 2) {
			int ny = y + dy;
			if (ny < 0 || ny >= t->h) continue;

			size_t nrow = (size_t)ny * t->w;
			bool   run  = false;

			for (int i = x0; i <= x1; i++) {
				bool match = MATCH(nrow, i);
				if (match && !run && top < cap) {
					stack[top * 2] = i; stack[top * 2 + 1] = ny; top++;
				}
				run = match;
			}
		}
		#undef MATCH
	}

	SDL_free(stack);
}

/*
 * The spray: random points inside a circle, the grain and the count from the first
 * Vangopix's tool_spray. It is measured in TIME - held still, it goes on building up, which
 * is the whole behaviour of a spray can and the reason this one tool has a per-frame job.
 */
#define SPRAY_GRAIN 5

static void plot_spray (VNG_TAB *t, int cx, int cy, int r)
{
	if (r < 1) r = 1;

	for (int i = 0; i < r; i++) {
		for (int j = 0; j < r; j++) {
			int dx = SDL_rand(SPRAY_GRAIN * r + 1) - r;
			int dy = SDL_rand(SPRAY_GRAIN * r + 1) - r;

			if (dx * dx + dy * dy < r * r)
				put(t, cx + dx, cy + dy);
		}
	}
}

/*
 * Change-colours: every pixel of the colour under the pointer becomes the current colour.
 *
 * THE LIMITER IS WHAT MAKES IT A TOOL RATHER THAN A MENU COMMAND. The whole sheet at size
 * one, or only inside a circle or a square when there is a size to work with: replacing one
 * shade of a sprite everywhere is one gesture, and replacing it only where a shadow falls is
 * the same gesture with a shape around it.
 */
static void plot_change (VNG_TAB *t, int cx, int cy)
{
	if (!inside(t, cx, cy)) return;

	Uint32 target = t->pixels[(size_t)cy * t->w + cx];
	if (target == laying) return;

	int r = size[T_CHANGE];

	int x0 = 0, y0 = 0, x1 = t->w - 1, y1 = t->h - 1;
	if (limiter != 0) {
		x0 = cx - r; x1 = cx + r;
		y0 = cy - r; y1 = cy + r;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > t->w - 1) x1 = t->w - 1;
		if (y1 > t->h - 1) y1 = t->h - 1;
	}

	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			if (limiter == 1) {
				int dx = x - cx, dy = y - cy;
				if (dx * dx + dy * dy > r * r) continue;
			}
			if (t->pixels[(size_t)y * t->w + x] == target)
				put(t, x, y);
		}
	}
}

void tool_fill (VNG_TAB *t, int x, int y, int slot, bool barrier)
{
	if (!t || !inside(t, x, y)) return;
	if (!vng_tab_stroke_open(t)) return;

	laying = colour[slot == 1 ? 1 : 0];
	plot_flood(t, x, y, barrier);
	vng_tab_stroke_close(t);
}

/* Everything a press or a drag lays down, in one place, so the event handler stays a list of
 * gestures instead of a list of tools. */
static void apply (VNG_TAB *t, int x, int y)
{
	switch (current) {
	case T_PENCIL:
	case T_ERASER:  plot_line(t, last_x, last_y, x, y);        break;
	case T_SPRAY:   plot_spray(t, x, y, size[T_SPRAY]);        break;
	case T_LINE:
		/* SHIFT snaps the far end to the pixel-art slopes before anything is drawn, so the
		 * preview and the committed line are the same line. */
		if (keys_mods() & SDL_KMOD_SHIFT) tool_snap_iso(anchor_x, anchor_y, &x, &y);
		plot_line(t, anchor_x, anchor_y, x, y);
		break;
	case T_RECT:    plot_rect(t, anchor_x, anchor_y, x, y);    break;
	case T_ELLIPSE: plot_ellipse(t, anchor_x, anchor_y, x, y); break;
	case T_CHANGE:  plot_change(t, x, y);                      break;
	default: break;
	}
}

/* ------------------------------------------------------------------------ the events */

/* Q W E R / A S D F, the first Vangopix's own block under the left hand. */
static bool tool_key (SDL_Keycode k, TOOL *out)
{
	switch (k) {
	case SDLK_Q: *out = T_PENCIL;  return true;
	case SDLK_W: *out = T_LINE;    return true;
	case SDLK_E: *out = T_RECT;    return true;
	case SDLK_R: *out = T_ELLIPSE; return true;
	case SDLK_A: *out = T_ERASER;  return true;
	case SDLK_S: *out = T_BUCKET;  return true;
	case SDLK_D: *out = T_SPRAY;   return true;
	case SDLK_F: *out = T_CHANGE;  return true;
	default: return false;
	}
}

bool tool_event (const SDL_Event *e, VNG_TAB *t)
{
	if (!t) return false;

	switch (e->type) {

	case SDL_EVENT_KEY_DOWN: {
		if (e->key.repeat) return false;

		SDL_Keymod m = e->key.mod;
		bool bare = (m & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0;

		/* A TOOL KEY ONLY COUNTS BARE. CTRL+S saves and S is the bucket; the two are told
		 * apart by asking whether ANY modifier is down, not by asking about the one that
		 * happens to collide today. */
		TOOL want;
		if (bare && tool_key(e->key.key, &want)) {
			current = want;
			return true;
		}

		/*
		 * SHIFT+TAB swaps the change-colours limiter between a circle and a square. The
		 * first Vangopix used bare TAB; bare TAB raises the project sidebar here, and a
		 * shortcut only one tool answers must not take a key the whole program can see.
		 */
		if (e->key.key == SDLK_TAB && (m & SDL_KMOD_SHIFT) && !(m & SDL_KMOD_CTRL)) {
			if (current == T_CHANGE && size[T_CHANGE] > 1) {
				limiter = (limiter == 1) ? 2 : 1;
				return true;
			}
			return false;
		}
		return false;
	}

	case SDL_EVENT_MOUSE_WHEEL: {
		/* SHIFT+wheel is the tip size, per tool and by that tool's own step. The plain wheel
		 * is the camera's, and view.c hands this one over rather than zooming. */
		if (!(keys_mods() & SDL_KMOD_SHIFT)) return false;

		size[current] += e->wheel.integer_y * step[current];
		if (size[current] < 1)       size[current] = 1;
		if (size[current] > TIP_MAX) size[current] = TIP_MAX;

		/* At size one there is no shape to limit with: it is the whole sheet. */
		if (current == T_CHANGE)
			limiter = (size[T_CHANGE] == 1) ? 0 : (limiter ? limiter : 1);

		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		if (e->button.button != SDL_BUTTON_LEFT &&
		    e->button.button != SDL_BUTTON_RIGHT) return false;

		int x, y;
		pixel_of(t, e->button.x, e->button.y, &x, &y);

		/* A press that starts OUTSIDE the sheet is not a drawing gesture - it is a click on
		 * the desk, and one day it will be a selection. */
		if (!inside(t, x, y)) return false;

		/* CTRL turns both buttons into the eyedropper, and while it is held neither is the
		 * tool's: a press that also drew would smear the colour being sampled across the
		 * very pixels being read. A drag already running is left alone - the stroke owns it,
		 * and CTRL pressed halfway through a line must not cut it. */
		if (!drawing && (keys_mods() & SDL_KMOD_CTRL)) {
			pick_slot = slot_of(e->button.button);
			picking   = true;
			button    = e->button.button;
			tool_pick(t, x, y, pick_slot);
			return true;
		}

		/* The bucket is a drop and not a drag: it opens, fills and closes in one gesture, and
		 * SHIFT chooses which of the two fills that is. */
		if (!drawing && current == T_BUCKET) {
			tool_fill(t, x, y, slot_of(e->button.button),
			          (keys_mods() & SDL_KMOD_SHIFT) != 0);
			return true;
		}

		/* A stroke already open means its button-up never arrived - the pointer was released
		 * somewhere this program never heard about. Commit it rather than merge the two:
		 * those pixels were drawn and the person watched them appear. */
		if (drawing) {
			drawing = false;
			vng_tab_stroke_close(t);
		}

		if (!vng_tab_stroke_open(t)) return true;   /* no memory for it; still ours */

		button   = e->button.button;
		laying   = (current == T_ERASER) ? 0u : colour[slot_of(button)];
		drawing  = true;
		last_x   = anchor_x = x;
		last_y   = anchor_y = y;

		if (current == T_PENCIL || current == T_ERASER) tip(t, x, y);
		else                                            apply(t, x, y);

		/* The bucket and the change do their whole job here. Leaving the stroke open would
		 * make a drag across the sheet refill on every motion event. */
		if (instant(current)) {
			drawing = false;
			vng_tab_stroke_close(t);
		}
		return true;
	}

	case SDL_EVENT_MOUSE_MOTION: {
		/* A held pick keeps absorbing as it travels, which is what picking constantly looks
		 * like: press, drag across the colours, release on the one you meant. Nothing is
		 * written to the document, so there is nothing to undo. */
		if (picking) {
			int px, py;
			pixel_of(t, e->motion.x, e->motion.y, &px, &py);
			tool_pick(t, px, py, pick_slot);
			return true;
		}

		if (!drawing) return false;

		int x, y;
		pixel_of(t, e->motion.x, e->motion.y, &x, &y);

		/* A SHAPE IS REDRAWN, NOT ACCUMULATED. The line being dragged is the line from the
		 * anchor to here, and the one from the previous motion event never existed. Throwing
		 * the preview away first is what makes that true - and the undo step stays open
		 * across it, so the whole drag is still one undo. */
		if (anchored(current))
			vng_tab_stroke_reset(t);

		apply(t, x, y);

		last_x = x;
		last_y = y;
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_UP: {
		if (picking && e->button.button == button) {
			picking = false;
			return true;
		}

		/* Only the button that started it ends it, so a second button pressed and let go
		 * mid-stroke does not close somebody else's stroke. */
		if (!drawing || e->button.button != button) return false;

		drawing = false;
		vng_tab_stroke_close(t);
		return true;
	}

	default:
		return false;
	}
}

void tool_frame (VNG_TAB *t)
{
	/* The spray is the one tool measured in time rather than in events: held still, it goes
	 * on building up, which is what a spray can does. Everything else has already happened
	 * by the time this runs. */
	if (t && drawing && current == T_SPRAY)
		plot_spray(t, last_x, last_y, size[T_SPRAY]);
}

/* ------------------------------------------------------------------------ the pixels */

/* The panels float OVER the sheet, so the pointer can be on the paper and on a panel at the
 * same time. Their own events already settle a click - they consume it before this file sees
 * it - but the cursor SHAPE is decided every frame, from where the pointer is, and it has to
 * agree with who would actually get the click. */
static bool over_panel (float mx, float my)
{
	return my < tabbar_height() || mx < sidebar_edge();
}

static void set_cursor (SDL_Cursor *want)
{
	if (!want || want == cur_now) return;
	SDL_SetCursor(want);
	cur_now = want;
}

/* White just inside, black one further out. Two rects because one is not enough: the sheet
 * can be any colour and the desk behind it is grey, so a single tone disappears against
 * something. Outside the pixels themselves - the same rule the corner grips and the sheet's
 * own frame follow, and here it is the whole point, since a box drawn ON a pixel hides the
 * colour it is asking about. */
static void box (SDL_FRect in)
{
	SDL_FRect out = { in.x - 1.0f, in.y - 1.0f, in.w + 2.0f, in.h + 2.0f };

	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xC0);
	SDL_RenderRect(vng_ren, &out);
	SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xE0);
	SDL_RenderRect(vng_ren, &in);
}

/* A circle in SCREEN space, for the change-colours limiter. Screen space and not document
 * space on purpose: it is an annotation about the tool, not something being drawn, so it
 * stays a smooth ring at any zoom instead of turning into a staircase. */
static void ring (float cx, float cy, float r)
{
	enum { SEG = 48 };
	SDL_FPoint p[SEG + 1];

	for (int i = 0; i <= SEG; i++) {
		float a = (float)i * (2.0f * SDL_PI_F / SEG);
		p[i].x = cx + SDL_cosf(a) * r + 1.0f;
		p[i].y = cy + SDL_sinf(a) * r + 1.0f;
	}

	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xC0);
	SDL_RenderLines(vng_ren, p, SEG + 1);

	for (int i = 0; i <= SEG; i++) { p[i].x -= 1.0f; p[i].y -= 1.0f; }
	SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xE0);
	SDL_RenderLines(vng_ren, p, SEG + 1);
}

/*
 * A word beside the pointer, for a variant a shape cannot show. The bucket looks exactly the
 * same whether SHIFT is held or not, and it does something quite different - so it says so.
 * The first Vangopix printed BARRIER at (+32, -32) for the same reason; the offset here is
 * double the glyph's, which puts it just clear of it.
 */
static void label (const char *text, float mx, float my)
{
	if (!vng_text) return;

	float tw, th;
	text_measure(vng_text, text, &tw, &th);

	SDL_FRect r = { mx + GLYPH_OFF_X * 2.0f, my + GLYPH_OFF_Y * 2.0f,
	                tw + READ_PAD * 2.0f, th + 2.0f };

	if (r.y < tabbar_height()) r.y = my - GLYPH_OFF_Y * 2.0f;
	if (r.x + r.w > vng_win_w) r.x = mx - GLYPH_OFF_X * 2.0f - r.w;

	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xC0);
	SDL_RenderFillRect(vng_ren, &r);
	text_print(vng_text, r.x + READ_PAD, r.y + 1.0f, 0xFFD060FF, "%s", text);
}

/*
 * The tip, outlined where it would land. This is what makes a size worth having: a tip whose
 * extent cannot be seen until it is used is a tip nobody trusts.
 *
 * The change-colours limiter is drawn as its actual SHAPE, circle or square, because
 * SHIFT+TAB swaps between the two and a swap nobody can see is a swap nobody will use.
 */
static void outline (VNG_TAB *t, int px, int py)
{
	int r = size[current];

	SDL_FPoint a = view_world_to_screen(t, (float)px, (float)py);
	SDL_FPoint b = view_world_to_screen(t, (float)px + 1.0f, (float)py + 1.0f);
	float cell = b.x - a.x;

	if (current == T_CHANGE && limiter == 1) {
		ring(a.x + cell * 0.5f, a.y + cell * 0.5f, (float)r * cell);
		return;
	}

	/* One pixel, or the square the tip covers. The eraser is odd-sized by definition, and
	 * the round tips are measured the same way, so the box says how far each reaches. */
	float half = (r <= 1) ? 0.0f : (float)r;
	if (current == T_ERASER) half = (float)((size[T_ERASER] | 1) / 2);

	SDL_FRect in = { a.x - half * cell, a.y - half * cell,
	                 cell * (half * 2.0f + 1.0f), cell * (half * 2.0f + 1.0f) };
	box(in);
}

/*
 * One bar: a colour with its value written inside it. Used for the readout that follows the
 * pointer and for the two loaded colours at the bottom, because they are the same question
 * asked about different colours.
 */
static void bar_draw (SDL_FRect bar, Uint32 argb)
{
	Uint8 a = (Uint8)((argb >> 24) & 0xFF);
	Uint8 r = (Uint8)((argb >> 16) & 0xFF);
	Uint8 g = (Uint8)((argb >>  8) & 0xFF);
	Uint8 b = (Uint8)( argb        & 0xFF);

	/* Two tones behind it, then the colour over them at its REAL alpha - the desk's own
	 * trick. It is what makes "nothing" look like nothing instead of looking like black, and
	 * a bar filled at alpha zero would simply not be there. */
	SDL_FRect half = { bar.x, bar.y, bar.w * 0.5f, bar.h };
	SDL_SetRenderDrawColor(vng_ren, 0x25, 0x25, 0x25, 0xFF);
	SDL_RenderFillRect(vng_ren, &bar);
	SDL_SetRenderDrawColor(vng_ren, 0x33, 0x33, 0x33, 0xFF);
	SDL_RenderFillRect(vng_ren, &half);

	SDL_SetRenderDrawColor(vng_ren, r, g, b, a);
	SDL_RenderFillRect(vng_ren, &bar);

	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xB0);
	SDL_RenderRect(vng_ren, &bar);

	if (!vng_text) return;

	/*
	 * Black or white BY LUMINANCE, not by inverting the colour. Inverting is the obvious
	 * trick and it fails exactly in the middle, where a great deal of pixel art lives: the
	 * inverse of 0x808080 is 0x7F7F7F, one step from the background it is meant to stand out
	 * from.
	 *
	 * Measured on the colour AS COMPOSITED over the tones behind it, so a transparent slot is
	 * judged against the grey actually there. Rec. 601 and not the average of the three
	 * channels, because an average calls saturated blue bright and saturated green dim.
	 */
	int er = (r * a + 0x2C * (255 - a)) / 255;
	int eg = (g * a + 0x2C * (255 - a)) / 255;
	int eb = (b * a + 0x2C * (255 - a)) / 255;
	int luma = (77 * er + 150 * eg + 29 * eb) >> 8;

	char hex[16];
	tool_hex(argb, hex, sizeof hex);

	text_print(vng_text, bar.x + READ_PAD, bar.y + 1.0f,
	           luma > 140 ? 0x000000FF : 0xFFFFFFFF, "%s", hex);
}

/* What a bar has to be to hold eight hex digits. Measured rather than assumed, so it still
 * fits if the face is ever swapped again. */
static void bar_size (float *w, float *h)
{
	float tw = 64.0f, th = 15.0f;
	if (vng_text) text_measure(vng_text, "88888888", &tw, &th);

	*w = tw + READ_PAD * 2.0f;
	*h = th + 2.0f;
}

/* The colour under the pointer while CTRL is held: the answer to "what would I get", which
 * is what makes a deliberate press worth making. */
static void preview (Uint32 argb, float mx, float my)
{
	float bw, bh;
	bar_size(&bw, &bh);

	SDL_FRect bar = { mx + READ_OFF_X, my + READ_OFF_Y, bw, bh };

	/* Near the right or bottom edge it flips to the other side of the pointer rather than
	 * sliding off the window, where the one thing it exists to say cannot be read. */
	if (bar.x + bar.w > vng_win_w) bar.x = mx - READ_OFF_X - bar.w;
	if (bar.y + bar.h > vng_win_h) bar.y = my - READ_OFF_Y - bar.h;

	bar_draw(bar, argb);
}

/*
 * The two loaded colours, bottom left, in button order: colour 1 then colour 2.
 *
 * They are always there because there is no other way to know which colour each side of the
 * mouse is holding, and that is not a question a person should have to press a key to ask.
 *
 * They STEP ASIDE for the project sidebar rather than being covered by it. sidebar_edge is
 * animated, so they slide along with the panel for free - and because the panel floats over
 * the sheet, the alternative was two bars hidden behind it at exactly the moment somebody is
 * choosing which file to work on.
 */
static void slots_draw (void)
{
	float bw, bh;
	bar_size(&bw, &bh);

	float x = sidebar_edge() + SLOT_MARGIN;
	float y = vng_win_h - bh - SLOT_MARGIN;

	for (int i = 0; i < 2; i++) {
		SDL_FRect bar = { x + i * (bw + SLOT_GAP), y, bw, bh };
		bar_draw(bar, colour[i]);
	}
}

void tool_draw (VNG_TAB *t)
{
	if (!t) return;

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	int x, y;
	pixel_of(t, mx, my, &x, &y);

	/* While a stroke is running the pointer owns the sheet wherever it has wandered to,
	 * including over a panel: letting the cursor flicker back to an arrow mid-stroke would
	 * report that the drawing had stopped, which it has not. */
	bool on = drawing || (inside(t, x, y) && !over_panel(mx, my));

	/* keys_mods reports nothing while a text field owns the keyboard, so typing a CTRL
	 * shortcut into one cannot turn the pointer into an eyedropper over the drawing. */
	bool eyedropper = !drawing && on && inside(t, x, y) &&
	                  (keys_mods() & SDL_KMOD_CTRL);

	set_cursor(on ? cur_cross : cur_arrow);

	/* A tip bigger than one pixel is outlined at any zoom, because its SIZE is what has to be
	 * visible before it is used. A single pixel needs the zoom to be worth outlining. */
	if (on && inside(t, x, y) && (t->zoom >= OUTLINE_ZOOM || size[current] > 1))
		outline(t, x, y);

	/*
	 * The tool glyph, which says WHICH tool without standing on the pixel. The system
	 * crosshair stays where it is and marks the aim point; the glyph hangs off it. The first
	 * Vangopix had both at once for the same reason - one answers "where", the other answers
	 * "what".
	 *
	 * It flips below the pointer near the top of the window, where hanging upward would put
	 * it off screen or behind the tab bar. The original did not bother; a window can be small
	 * enough that it matters.
	 */
	if (on) {
		float gy = my + GLYPH_OFF_Y;
		if (gy - GLYPH_REACH < tabbar_height())
			gy = my - GLYPH_OFF_Y;

		glyph_draw((eyedropper || picking) ? GLYPH_PICK : (GLYPH)current,
		           mx + GLYPH_OFF_X, gy, 1.0f, 0xFFFFFFFF);
	}

	/* Over the outline: the value being read matters more than the box saying which pixel it
	 * came from. A pick being dragged keeps the readout up, showing what was just absorbed -
	 * the same pixel either way. */
	if ((eyedropper || picking) && inside(t, x, y))
		preview(t->pixels[(size_t)y * t->w + x], mx, my);

	/* The bucket's two fills look identical until one of them runs, so the variant is named
	 * while the modifier that chooses it is held. */
	if (on && !eyedropper && current == T_BUCKET && (keys_mods() & SDL_KMOD_SHIFT))
		label("barrier", mx, my);

	slots_draw();
}
