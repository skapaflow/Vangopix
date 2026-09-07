#include "tool.h"
#include "view.h"
#include "keys.h"
#include "tabbar.h"
#include "sidebar.h"

/*
 * Where the pencil starts, and what the right button lays down. 0xAARRGGBB, matching the
 * ARGB8888 the document is. INK is only the FIRST colour - CTRL replaces it with whatever
 * is under the pointer, which is how a colour is chosen here.
 *
 * ERASING GOES TO TRANSPARENT AND NOT TO WHITE, which is the whole reason the desk is a
 * checkerboard: in a program that keeps alpha, white is a colour somebody chose and
 * nothing is the absence of one. Rubbing out to white would be undoing the drawing by
 * painting over it.
 */
#define INK    0xFF000000u
#define ERASE  0x00000000u

/* Below this the outline would cover more of the sheet than it points at - at 1:1 a pixel
 * is one screen pixel and a box around it swallows its eight neighbours. Under it the
 * crosshair is the only signal, which is right: at that scale the image is being LOOKED
 * at, not drawn on. */
#define OUTLINE_ZOOM  2.0f

/* How far off the pointer the readout sits, and the padding inside its bar. Offset for the
 * same reason the first Vangopix offset its tool glyph: a thing that reports what you are
 * pointing at must not stand on it. */
#define READ_OFF_X  14.0f
#define READ_OFF_Y  16.0f
#define READ_PAD     4.0f

static SDL_Cursor *cur_cross = NULL;
static SDL_Cursor *cur_arrow = NULL;
static SDL_Cursor *cur_now   = NULL;   /* what is on screen, so the OS is not asked twice */

/* The chosen colour SURVIVES a stroke - it is the pencil's, not the drag's. `laying` is
 * what the current drag is putting down, which is the colour or, on the right button,
 * nothing at all. */
static Uint32 colour = INK;
static Uint32 laying = INK;

static bool   drawing = false;
static Uint8  button  = 0;             /* the one that started the stroke */
static int    last_x  = 0, last_y = 0;

Uint32 tool_colour (void) { return colour; }

void tool_pick (VNG_TAB *t, int x, int y)
{
	if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return;

	/* From the document and never from the preview: what is absorbed is a colour that is
	 * IN the drawing, not one that is still on its way in. */
	colour = t->pixels[(size_t)y * t->w + x];
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
	/* The cursor on screen is destroyed with the rest, so the system is put back on its
	 * own before either of ours goes away. */
	if (cur_arrow) SDL_SetCursor(cur_arrow);

	if (cur_cross) SDL_DestroyCursor(cur_cross);
	if (cur_arrow) SDL_DestroyCursor(cur_arrow);
	cur_cross = cur_arrow = cur_now = NULL;
}

/*
 * Screen point to document pixel.
 *
 * SDL_floorf and NOT a cast to int: a cast truncates toward zero, so a world coordinate
 * of -0.5 - the pointer just off the left edge - comes out as pixel 0 instead of -1, and
 * a stroke leaving the sheet on that side would smear along the first column instead of
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

/*
 * Bresenham between two samples, and it is not a refinement.
 *
 * A hand moving at any speed outruns the motion events: the pointer arrives eight pixels
 * from where it was, and a tool that paints only where the events land draws a dotted
 * line. Every pixel editor joins the samples, and this is that join.
 *
 * The touched check is what the mask is FOR. With an opaque ink it changes nothing, but a
 * half transparent brush passing over its own line inside one stroke would blend onto its
 * own output and saturate the transparency into opacity - the detail the first Vangopix
 * documents at the top of its main.c.
 */
static void line (VNG_TAB *t, int x0, int y0, int x1, int y1)
{
	int dx =  SDL_abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -SDL_abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	for (;;) {
		/* No bounds test here on purpose: a stroke that runs off the sheet and comes
		 * back is ONE stroke, and the line between two samples outside it still has to
		 * be walked. vng_tab_put clips, so the part that lands is the part that lands. */
		if (!vng_tab_touched(t, x0, y0))
			vng_tab_put(t, x0, y0, laying);

		if (x0 == x1 && y0 == y1) break;

		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

bool tool_event (const SDL_Event *e, VNG_TAB *t)
{
	if (!t) return false;

	switch (e->type) {

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		if (e->button.button != SDL_BUTTON_LEFT &&
		    e->button.button != SDL_BUTTON_RIGHT) return false;

		/* CTRL is the eyedropper, and while it is held the buttons are not the pencil's.
		 * A press that also drew would smear the colour being sampled across the very
		 * pixels being read. A drag already running is not touched: the stroke owns it. */
		if (!drawing && (keys_mods() & SDL_KMOD_CTRL)) return false;

		int x, y;
		pixel_of(t, e->button.x, e->button.y, &x, &y);

		/* A press that starts OUTSIDE the sheet is not a drawing gesture - it is a click
		 * on the desk, and one day it will be a selection. Only presses on the paper
		 * begin a stroke. */
		if (!inside(t, x, y)) return false;

		/* A stroke already open means its button-up never arrived - the pointer was
		 * released somewhere this program never heard about. Commit it rather than merge
		 * the two: those pixels were drawn and the person watched them appear. */
		if (drawing) {
			drawing = false;
			vng_tab_stroke_close(t);
		}

		if (!vng_tab_stroke_open(t)) return true;   /* no memory for it; still ours */

		button  = e->button.button;
		laying  = (button == SDL_BUTTON_RIGHT) ? ERASE : colour;
		drawing = true;
		last_x  = x;
		last_y  = y;

		vng_tab_put(t, x, y, laying);
		return true;
	}

	case SDL_EVENT_MOUSE_MOTION: {
		if (!drawing) return false;

		int x, y;
		pixel_of(t, e->motion.x, e->motion.y, &x, &y);

		line(t, last_x, last_y, x, y);
		last_x = x;
		last_y = y;
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_UP: {
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

/* The panels float OVER the sheet, so the pointer can be on the paper and on a panel at
 * the same time. Their own events already settle a click - they consume it before this
 * file sees it - but the cursor SHAPE is decided every frame, from where the pointer is,
 * and it has to agree with who would actually get the click. */
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

/* White just outside the pixel and black one further out. Two rects because one is not
 * enough: the sheet can be any colour and the desk behind it is grey, so a single tone
 * disappears against something. Outside and never on the pixel - the same rule the corner
 * grips and the sheet's own frame follow, and here it is the whole point, since a box
 * drawn ON a pixel hides the colour it is asking about. */
static void outline (VNG_TAB *t, int px, int py)
{
	SDL_FPoint a = view_world_to_screen(t, (float)px,       (float)py);
	SDL_FPoint b = view_world_to_screen(t, (float)px + 1.0f, (float)py + 1.0f);

	SDL_FRect in  = { a.x - 1.0f, a.y - 1.0f, b.x - a.x + 2.0f, b.y - a.y + 2.0f };
	SDL_FRect out = { in.x - 1.0f, in.y - 1.0f, in.w + 2.0f, in.h + 2.0f };

	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xC0);
	SDL_RenderRect(vng_ren, &out);
	SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xE0);
	SDL_RenderRect(vng_ren, &in);
}

/*
 * The hex readout, up only while CTRL is held.
 *
 * THE SWATCH IS FILLED OPAQUE EVEN WHEN THE COLOUR IS NOT. A transparent pick drawn at its
 * own alpha is an invisible bar, which reports nothing; the alpha is in the text, where it
 * can be read - and reading `...00` is how a person learns they just absorbed nothing.
 *
 * The text is black or white BY LUMINANCE, not by inverting the swatch. Inverting is the
 * obvious trick and it fails exactly in the middle, which is where a great deal of pixel
 * art lives: the inverse of 0x808080 is 0x7F7F7F, one step from the background it is meant
 * to stand out from.
 */
static void readout (Uint32 argb, float mx, float my)
{
	if (!vng_text) return;

	char hex[16];
	tool_hex(argb, hex, sizeof hex);

	float tw, th;
	text_measure(vng_text, hex, &tw, &th);

	SDL_FRect bar = { mx + READ_OFF_X, my + READ_OFF_Y,
	                  tw + READ_PAD * 2.0f, th + 2.0f };

	/* Near the right or bottom edge the bar flips to the other side of the pointer rather
	 * than sliding off the window, where the one thing it exists to say cannot be read. */
	if (bar.x + bar.w > vng_win_w) bar.x = mx - READ_OFF_X - bar.w;
	if (bar.y + bar.h > vng_win_h) bar.y = my - READ_OFF_Y - bar.h;

	Uint8 r = (Uint8)((argb >> 16) & 0xFF);
	Uint8 g = (Uint8)((argb >>  8) & 0xFF);
	Uint8 b = (Uint8)( argb        & 0xFF);

	SDL_SetRenderDrawColor(vng_ren, r, g, b, 0xFF);
	SDL_RenderFillRect(vng_ren, &bar);

	/* A border, or a white swatch on a pale drawing is no bar at all. */
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xB0);
	SDL_RenderRect(vng_ren, &bar);

	/* Rec. 601 luma, not the average of the three: the eye is not equally sensitive to
	 * them, and an average calls saturated blue bright and saturated green dim. The
	 * threshold sits a little above the middle because white text carries further on a
	 * mid tone than black does. */
	int luma = (77 * r + 150 * g + 29 * b) >> 8;

	text_print(vng_text, bar.x + READ_PAD, bar.y + 1.0f,
	           luma > 140 ? 0x000000FF : 0xFFFFFFFF, "%s", hex);
}

void tool_draw (VNG_TAB *t)
{
	if (!t) return;

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	int x, y;
	pixel_of(t, mx, my, &x, &y);

	/* While a stroke is running the pointer owns the sheet wherever it has wandered to,
	 * including over a panel: letting the cursor flicker back to an arrow mid-stroke
	 * would report that the drawing had stopped, which it has not. */
	bool on = drawing || (inside(t, x, y) && !over_panel(mx, my));

	/*
	 * The pick is decided PER FRAME rather than from an event, and that is what makes it
	 * absorb on hover. An event-driven version would need CTRL-down and every motion
	 * afterwards, and would still miss the case of CTRL pressed while the hand is holding
	 * still - which is most of them.
	 *
	 * keys_mods returns nothing while a text field owns the keyboard, so typing into one
	 * cannot start sampling the drawing behind it.
	 */
	bool picking = !drawing && on && inside(t, x, y) &&
	               (keys_mods() & SDL_KMOD_CTRL);

	if (picking) tool_pick(t, x, y);

	set_cursor(on ? cur_cross : cur_arrow);

	if (on && t->zoom >= OUTLINE_ZOOM && inside(t, x, y))
		outline(t, x, y);

	/* Last, so it sits over the outline: the value being read matters more than the box
	 * saying which pixel it came from. */
	if (picking) readout(colour, mx, my);
}
