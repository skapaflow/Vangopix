#include "tool.h"
#include "view.h"
#include "keys.h"
#include "tabbar.h"
#include "sidebar.h"

/*
 * The two colours a fresh program starts with. 0xAARRGGBB, matching the ARGB8888 the
 * document is.
 *
 * COLOUR 2 IS NOTHING, and that is what makes the right button an eraser without an eraser
 * existing. In a program that keeps alpha, rubbing out IS drawing with nothing - and white
 * would be a colour somebody chose, which is the whole reason the desk is a checkerboard.
 */
#define START_1  0xFF000000u
#define START_2  0x00000000u

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

/* The loaded colours sit this far from the corner, with this much between them. */
#define SLOT_MARGIN  8.0f
#define SLOT_GAP     6.0f

static SDL_Cursor *cur_cross = NULL;
static SDL_Cursor *cur_arrow = NULL;
static SDL_Cursor *cur_now   = NULL;   /* what is on screen, so the OS is not asked twice */

/* The two loaded colours SURVIVE a stroke - they are the pencil's, not the drag's.
 * `laying` is what the current drag is putting down, copied at the press so that picking
 * mid-stroke could never change the colour of a line already begun. */
static Uint32 colour[2] = { START_1, START_2 };
static Uint32 laying    = START_1;

static bool   drawing = false;
static Uint8  button  = 0;             /* the one that started the stroke */
static int    last_x  = 0, last_y = 0;

/* True while a CTRL press is being dragged across the sheet, absorbing as it goes. */
static bool   picking = false;
static int    pick_slot = 0;

/* Which slot a button owns. The button that takes a colour is the button that lays it
 * down, so there is nothing to remember about where a pick landed. */
static int slot_of (Uint8 btn) { return btn == SDL_BUTTON_RIGHT ? 1 : 0; }

Uint32 tool_colour (int slot) { return colour[slot == 1 ? 1 : 0]; }

void tool_pick (VNG_TAB *t, int x, int y, int slot)
{
	if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return;

	/* From the document and never from the preview: what is absorbed is a colour that is
	 * IN the drawing, not one still on its way in. */
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

		int x, y;
		pixel_of(t, e->button.x, e->button.y, &x, &y);

		/* A press that starts OUTSIDE the sheet is not a drawing gesture - it is a click
		 * on the desk, and one day it will be a selection. Only presses on the paper
		 * begin a stroke, or a pick. */
		if (!inside(t, x, y)) return false;

		/* CTRL turns both buttons into the eyedropper, and while it is held neither is
		 * the pencil's: a press that also drew would smear the colour being sampled
		 * across the very pixels being read. A drag already running is left alone - the
		 * stroke owns it, and CTRL pressed halfway through a line must not cut it. */
		if (!drawing && (keys_mods() & SDL_KMOD_CTRL)) {
			pick_slot = slot_of(e->button.button);
			picking   = true;
			button    = e->button.button;
			tool_pick(t, x, y, pick_slot);
			return true;
		}

		/* A stroke already open means its button-up never arrived - the pointer was
		 * released somewhere this program never heard about. Commit it rather than merge
		 * the two: those pixels were drawn and the person watched them appear. */
		if (drawing) {
			drawing = false;
			vng_tab_stroke_close(t);
		}

		if (!vng_tab_stroke_open(t)) return true;   /* no memory for it; still ours */

		button  = e->button.button;
		laying  = colour[slot_of(button)];
		drawing = true;
		last_x  = x;
		last_y  = y;

		vng_tab_put(t, x, y, laying);
		return true;
	}

	case SDL_EVENT_MOUSE_MOTION: {
		/* A held pick keeps absorbing as it travels, which is what "picking constantly"
		 * actually looks like: press, drag across the colours, release on the one you
		 * meant. Nothing is written to the document, so there is nothing to undo. */
		if (picking) {
			int x, y;
			pixel_of(t, e->motion.x, e->motion.y, &x, &y);
			tool_pick(t, x, y, pick_slot);
			return true;
		}

		if (!drawing) return false;

		int x, y;
		pixel_of(t, e->motion.x, e->motion.y, &x, &y);

		line(t, last_x, last_y, x, y);
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
static void bar_draw (SDL_FRect bar, Uint32 argb)
{
	Uint8 a = (Uint8)((argb >> 24) & 0xFF);
	Uint8 r = (Uint8)((argb >> 16) & 0xFF);
	Uint8 g = (Uint8)((argb >>  8) & 0xFF);
	Uint8 b = (Uint8)( argb        & 0xFF);

	/* Two tones behind it, then the colour over them at its REAL alpha - the desk's own
	 * trick. It is what makes "nothing" look like nothing instead of looking like black,
	 * and a bar filled at alpha zero would simply not be there. */
	SDL_FRect half = { bar.x, bar.y, bar.w * 0.5f, bar.h };
	SDL_SetRenderDrawColor(vng_ren, 0x25, 0x25, 0x25, 0xFF);
	SDL_RenderFillRect(vng_ren, &bar);
	SDL_SetRenderDrawColor(vng_ren, 0x33, 0x33, 0x33, 0xFF);
	SDL_RenderFillRect(vng_ren, &half);

	SDL_SetRenderDrawColor(vng_ren, r, g, b, a);
	SDL_RenderFillRect(vng_ren, &bar);

	/* A border, or a white swatch on a pale drawing is no bar at all. */
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xB0);
	SDL_RenderRect(vng_ren, &bar);

	if (!vng_text) return;

	/*
	 * Black or white BY LUMINANCE, not by inverting the colour. Inverting is the obvious
	 * trick and it fails exactly in the middle, where a great deal of pixel art lives: the
	 * inverse of 0x808080 is 0x7F7F7F, one step from the background it is meant to stand
	 * out from.
	 *
	 * Measured on the colour AS COMPOSITED over the tones behind it, so a transparent slot
	 * is judged against the grey actually there and not against a colour nobody can see.
	 * Rec. 601 and not the average of the three channels, because an average calls
	 * saturated blue bright and saturated green dim.
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

/* What a bar has to be to hold eight hex digits. Measured on the widest digits rather than
 * assumed, so it still fits if the face is ever swapped again. */
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
	 * including over a panel: letting the cursor flicker back to an arrow mid-stroke
	 * would report that the drawing had stopped, which it has not. */
	bool on = drawing || (inside(t, x, y) && !over_panel(mx, my));

	/* keys_mods reports nothing while a text field owns the keyboard, so typing a CTRL
	 * shortcut into one cannot turn the pointer into an eyedropper over the drawing. */
	bool eyedropper = !drawing && on && inside(t, x, y) &&
	                  (keys_mods() & SDL_KMOD_CTRL);

	set_cursor(on ? cur_cross : cur_arrow);

	if (on && t->zoom >= OUTLINE_ZOOM && inside(t, x, y))
		outline(t, x, y);

	/* Over the outline: the value being read matters more than the box saying which pixel
	 * it came from. A pick being dragged keeps the preview up, showing what was just
	 * absorbed - the same pixel either way. */
	if ((eyedropper || picking) && inside(t, x, y))
		preview(t->pixels[(size_t)y * t->w + x], mx, my);

	slots_draw();
}
