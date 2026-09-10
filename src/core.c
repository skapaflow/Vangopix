#include "core.h"
#include "ui.h"
#include "primitives.h"
#include "vangopix.h"
#include "tabs.h"
#include "tabbar.h"
#include "view.h"
#include "resize.h"
#include "sidebar.h"
#include "keymap.h"
#include "splash.h"
#include "project.h"
#include "keys.h"
#include "prompt.h"
#include "file.h"
#include "undo.h"
#include "tool.h"
#include "select.h"
#include "thumb.h"
#include "win.h"
#include "colour.h"
#include "palette.h"
#include "anim.h"

/*
 * The desk: a grey checkerboard, the size and the two greys taken from what the first
 * Vangopix had settled on in its config.ini.
 *
 * It is drawn in SCREEN space, not document space - it is the table the paper lies on,
 * so it must not slide when the camera pans, or it would read as part of the artwork.
 */
/* The numbers live in core.h now, because a shape SDL cannot clip to has to lay the same
 * pattern down by hand and the two must not drift apart.
 *
 * 0xAARRGGBB, to match the ARGB8888 the texture is created with. The first Vangopix wrote
 * these as 0xRRGGBBAA in its config.ini, and transcribing that shape straight into an RGBA32
 * texture is how this first came out red: RGBA32 orders the BYTES R,G,B,A, so a little endian
 * machine reads 0x252525FF back as red 0xFF with alpha 0x25. */
#define CHECK       VNG_CHECK
#define CHECK_A     VNG_CHECK_A
#define CHECK_B     VNG_CHECK_B

/*
 * ONE TILE PER DISTINCT BOARD, BUILT ONCE.
 *
 * There are two: the desk at six pixels in its greys, and the palette's swatch grid at five in
 * its own blues. There will never be many - the cap is small on purpose, so this stays a
 * lookup rather than becoming a cache with a policy.
 *
 * What must not fork is THE DESK, not every board. See vangopix_board_rect in core.h.
 */
#define TILES 4

static struct { int square; Uint32 a, b; SDL_Texture *tex; } tile[TILES];

/* A tile of 2x2 squares, uploaded once and repeated by the GPU.
 *
 * The obvious way - a filled rect per square - is about thirteen thousand draw calls a
 * frame at 800x600, every frame, for a backdrop that never changes. One tiled texture is
 * a single call, and it stays a single call at any window size. */
static SDL_Texture *tile_of (int square, Uint32 ca, Uint32 cb)
{
	if (square < 1) square = CHECK;

	for (int i = 0; i < TILES; i++)
		if (tile[i].tex && tile[i].square == square &&
		    tile[i].a == ca && tile[i].b == cb) return tile[i].tex;

	int slot = -1;
	for (int i = 0; i < TILES; i++)
		if (!tile[i].tex) { slot = i; break; }
	if (slot < 0) return NULL;

	const int n  = square * 2;
	Uint32   *px = (Uint32 *) SDL_malloc((size_t)n * n * sizeof(Uint32));
	if (!px) return NULL;

	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
			px[y * n + x] = ((x < square) == (y < square)) ? ca : cb;

	SDL_Texture *t = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
	                                   SDL_TEXTUREACCESS_STATIC, n, n);
	if (!t) {
		SDL_Log("checker: %s", SDL_GetError());
		SDL_free(px);
		return NULL;
	}
	/* Nearest, or the seam between two tiles blurs into a grey line at the joins. */
	SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
	SDL_UpdateTexture(t, NULL, px, n * (int)sizeof(Uint32));
	SDL_free(px);

	tile[slot].square = square;
	tile[slot].a      = ca;
	tile[slot].b      = cb;
	tile[slot].tex    = t;
	return t;
}

void vangopix_board_rect (SDL_FRect r, int square, Uint32 ca, Uint32 cb)
{
	SDL_Texture *t = tile_of(square, ca, cb);

	if (!t) {
		/* No texture is still a backdrop: the darker of the two flat, rather than a hole. */
		SDL_SetRenderDrawColor(vng_ren, (Uint8)((ca >> 16) & 0xFF), (Uint8)((ca >> 8) & 0xFF),
		                                (Uint8)(ca & 0xFF), 0xFF);
		SDL_RenderFillRect(vng_ren, &r);
		return;
	}
	SDL_RenderTextureTiled(vng_ren, t, NULL, 1.0f, &r);
}

void vangopix_desk_rect (SDL_FRect r)
{
	vangopix_board_rect(r, CHECK, CHECK_A, CHECK_B);
}

/*
 * A filled disc of one colour, over a backing that says what is transparent about it.
 *
 * THE BACKING IS THE DESK'S CHECKERBOARD, NOT THE BARS' TWO HALVES. Every swatch in this
 * program shows alpha by laying the colour over two tones, and everywhere else the swatch is
 * a RECTANGLE - where a split down the middle reads as the swatch convention it is. On a
 * CIRCLE it reads as the disc being broken in two: a straight line across the middle of a
 * round shape is a crack, because nothing about the shape explains it. The checkerboard has
 * no middle to split on, and it is what the sheet, the selection's hole, the 1:1 panel and
 * the desk itself all use - so a transparent colour means here what it means everywhere.
 *
 * The circle itself is primitives.c's now - see there for what went wrong when it was three
 * different circles at once. What stays here is the one part that cannot be a primitive: the
 * DESK laid inside the shape, which SDL cannot clip a tiled texture to.
 */
void vangopix_desk_disc (float fcx, float fcy, float fr, Uint32 argb, Uint32 rim)
{
	int ri = prim_round(fr);
	if (ri < 1) return;

	int cx = prim_round(fcx), cy = prim_round(fcy);
	int x0 = cx - ri, y0 = cy - ri;

	/*
	 * THE BOARD IS STEPPED OUT BY HAND, and this is the one thing here that primitives.c
	 * cannot do for us: SDL can clip a tiled texture to a rectangle and to nothing else, and
	 * a circle is not one. So the rows are walked - but they are prim_span's rows, the same
	 * ones prim_disc and prim_circle use, which is what stops this becoming a second circle
	 * that drifts from the first. The phase comes off the bounding box, which is the origin a
	 * tiled call would have anchored to.
	 */
	for (int dy = -ri; dy <= ri; dy++) {
		int h  = prim_span(ri, dy);
		int y  = cy + dy;
		int iy = (y - y0) / VNG_CHECK;
		int lo = cx - h, hi = cx + h + 1;   /* half open, so a width is hi - lo */

		for (int x = lo; x < hi; ) {
			int    ix   = (x - x0) / VNG_CHECK;
			int    next = x0 + (ix + 1) * VNG_CHECK;
			Uint32 t    = ((ix + iy) & 1) ? VNG_CHECK_B : VNG_CHECK_A;

			if (next > hi) next = hi;

			SDL_FRect r = { (float)x, (float)y, (float)(next - x), 1.0f };
			prim_fill(r, t);
			x = next;
		}
	}

	/* The colour over it at its REAL alpha - what makes nothing look like nothing - and then
	 * the rim, so a pale colour still has an edge against whatever it sits on. The caller
	 * chooses that colour: the wheel wants a dark hairline, a swatch wants one that reads
	 * against what it just drew. Both shapes come off prim_span, so the rim cannot miss. */
	prim_disc  (fcx, fcy, fr, argb);
	prim_circle(fcx, fcy, fr, rim);
}

static void draw_desk (void)
{
	SDL_FRect all = { 0.0f, 0.0f, (float)vng_win_w, (float)vng_win_h };
	vangopix_desk_rect(all);
}

void vangopix_core_free (void)
{
	for (int i = 0; i < TILES; i++) {
		if (tile[i].tex) SDL_DestroyTexture(tile[i].tex);
		tile[i].tex    = NULL;
		tile[i].square = 0;
		tile[i].a = tile[i].b = 0u;
	}
}

/* The overlay is OFF by default and it is not chrome: it occupies no space when it is
 * not asked for. F1 toggles it. */
static bool overlay = false;

/*
 * THE TWO PANELS ARE ONE KEY EACH, AND BOTH ARE BARE.
 *
 * TAB raises the project sidebar and ESC raises the tab bar. TAB went to the sidebar
 * because that is the panel a hand reaches for while working - it is the file list, and
 * it is opened and closed all day.
 *
 * It replaces a CTRL+ALT chord, and why that had to go is worth keeping: a shortcut made
 * only of modifiers cannot be recognised on the way DOWN, because while CTRL+ALT are
 * going down they are indistinguishable from the start of CTRL+ALT+something. So it
 * armed when both were held, disarmed on any other key or any mouse button, and fired
 * when the first of the two came back up while still armed. Three states and a disarm
 * list, for what a bare key does in one line.
 *
 * ESC took the tab bar because it is the key already on the way to the top left, and
 * because nothing here cancels: this program has no modal state to escape from, so ESC
 * was a key spent on nothing.
 *
 * CTRL+TAB still walks between documents. One key, two jobs, told apart by the modifier.
 */

/*
 * CTRL+N asks for a size instead of always making 64x64. It is the first thing in the
 * program that needs the keyboard to belong to something other than the shortcuts, which
 * is what keys.c is for.
 */
static void new_sheet (const char *text)
{
	int w = 0, h = 0;

	/* Whitespace in the format skips any, and the suppressed scanset takes the
	 * separator, so "64x64", "64 x 64", "64X64" and "64*64" all read. */
	int n = SDL_sscanf(text, "%d %*[xX*] %d", &w, &h);

	if (n == 1) h = w;    /* one number is a square, which is what one number means */
	else if (n != 2) return;

	/* Nothing is said about a refusal on purpose: the answer is on screen, so a person
	 * who typed 0 sees no new tab and types again. An error box would be a second
	 * window to dismiss for a mistake that costs nothing. */
	if (w < 1 || h < 1 || w > VNG_MAX_SIDE || h > VNG_MAX_SIDE) return;

	vng_tab_new(w, h);
}

static void new_sheet_ask (void)
{
	/* No font, no field - and then CTRL+N is what it always was rather than nothing. */
	if (!prompt_open("new sheet:  width x height", "64x64", new_sheet))
		vng_tab_new(VNG_NEW_W, VNG_NEW_H);
}

void vangopix_input (void)
{
	SDL_Event e;

	while (SDL_PollEvent(&e)) {

		/* The bar gets first refusal while it is up. It floats OVER the sheet, so
		 * without this a click meant for a tab would also land on the drawing
		 * underneath - and once tools exist, that is a stray pixel every time. */
		/*
		 * BEFORE EVEN RUNG 0, and only for as long as the program has not started.
		 *
		 * The splash covers everything, so it answers before everything - including the
		 * keyboard's owner, which is the one thing the layer chain already had above it.
		 * SDL_EVENT_QUIT still falls through: see splash.h.
		 */
		if (splash_event(&e))
			continue;

		/* RUNG 0, AND IT IS NOT PART OF THE LAYER CHAIN BELOW.
		 *
		 * The layers under this line are ordered by what is on top of the screen,
		 * because a click has a position and the thing drawn over another one has to
		 * answer for it first. A key has no position: nothing about TAB says whether it
		 * belongs to the sidebar or to a name being typed. So the keyboard is routed by
		 * OWNERSHIP instead, and an owner beats every layer - including view.c, whose
		 * space-pan would otherwise fire on a space typed into a field.
		 *
		 * Only keyboard events are offered here; everything else falls straight
		 * through, which is what leaves the mouse untouched by a field being open. */
		if (keys_event(&e))
			continue;

		/* Also outside the layer chain, and for a related reason: this one does not come
		 * from a person at all. It is the file dialog's answer, pushed from whatever
		 * thread the OS ran it on, and it belongs to whoever asked - not to whatever
		 * happens to be on top of the screen now. */
		if (file_event(&e))
			continue;

		if (tabbar_event(&e))
			continue;

		/* The sidebar sits under the tab bar and over everything else, and its claim is
		 * the same as the bar's: it floats over the sheet, so a click inside it must
		 * not also reach the drawing underneath. */
		if (sidebar_event(&e))
			continue;

		/*
		 * THE PALETTE UNDER THE HAND, which is up only while ALT is held - so while it is
		 * up it is the topmost thing on the screen, and it is drawn that way too. It takes
		 * only what lands on its own cells; a press anywhere else with ALT down is not a
		 * gesture it owns, and swallowing one would be swallowing a stroke.
		 */
		if (palette_quick_event(&e, vng_tab))
			continue;

		/* The floating windows. Under the bar and the panel above, because those two are
		 * summoned to be used and put away again, while a window is parked and stays - and a
		 * parked window must not hide the thing you raised the bar to reach. */
		if (win_event(&e))
			continue;

		/* THE PALETTE'S GRID OF SWATCHES, one rung under the windows because it is not one:
		 * it hangs off the palette box rather than living inside it, so win.c neither routes
		 * nor clips it and a window parked on top has to keep covering it. */
		if (palette_grid_event(&e, vng_tab))
			continue;

		/* Then the canvas grips, BEFORE the camera: they answer the left button, and
		 * so does the space-pan. Whichever runs first wins the drag, and grabbing a
		 * corner has to mean resizing it. */
		if (resize_event(&e, vng_tab))
			continue;

		/* Then the camera. It answers the wheel and the pan drag; anything it does not
		 * want falls through to the tool and then to the keys below. */
		if (view_event(&e, vng_tab))
			continue;

		/* The selection, just before the tool: while a float is being carried the pointer is
		 * its, and a drawing tool must not see those drags. */
		if (select_event(&e, vng_tab))
			continue;

		/* AND LAST, THE TOOL. It comes after everything that can claim the same button -
		 * a tab, a folder, a corner grip, the space-pan - because a stray pixel is the
		 * one mistake in this chain that lands in the artwork and stays there. */
		if (tool_event(&e, vng_tab))
			continue;

		switch (e.type) {

		case SDL_EVENT_QUIT:
			/* The window's X and ALT+F4 arrive here, and neither is a keyboard event -
			 * so they still work while a field owns the keyboard, which is what keeps a
			 * modal from being a way to trap somebody in the program. */
			if (file_confirm_quit())
				vng_loop = false;
			break;

		/* Dropping a file opens it in a new tab - the gesture that lets Vangopix act
		 * as the machine's image viewer without owning a File menu. */
		/* A FOLDER becomes a project, a FILE becomes a tab. One gesture, and which one
		 * it is comes from the path itself rather than from a mode the person has to
		 * remember being in. */
		case SDL_EVENT_DROP_FILE: {
			SDL_PathInfo info;
			if (SDL_GetPathInfo(e.drop.data, &info) &&
			    info.type == SDL_PATHTYPE_DIRECTORY) {
				if (project_add(e.drop.data) && !sidebar_visible())
					sidebar_toggle();   /* show what just arrived, or the drop looks
					                     * like it did nothing */
			} else {
				vng_tab_open(e.drop.data);
			}
			break;
		}

		case SDL_EVENT_KEY_DOWN:
			if (e.key.repeat) break;

			/*
			 * A BARE SHORTCUT IS ONE WITH NO MODIFIER AT ALL, and asking it that way is
			 * the fix for a real bug: the sidebar used to ask "is CTRL down?" when the
			 * question was "is this key bare?", so SHIFT+TAB - which belongs to the
			 * change-colours limiter - raised the project panel as well.
			 *
			 * Testing for the one modifier that happens to collide today is how every
			 * later collision gets built in. There is only one right question.
			 */
			/*
			 * THE BARE SHORTCUTS, ASKED BY NAME.
			 *
			 * Every one of these used to test a keycode here. They test an ACTION now, and
			 * the key each action sits on comes from keymap.c and from the keyboard.txt
			 * beside the executable - see keymap.h.
			 *
			 * Two things fell out with the keycodes and are worth naming, because they were
			 * the reason this block had a comment at all.
			 *
			 * The first is `bare`. It asked whether NO modifier was down rather than whether
			 * the one that collides today was - which was the fix for a real bug, SHIFT+TAB
			 * raising the project panel as well as swapping the change-colours limiter.
			 * keymap_hit asks it that same way, in one place, for the whole program.
			 *
			 * The second is that "which key" and "what it does" are no longer written on the
			 * same line. That is the point: the line below is what the program DOES, and the
			 * table in keymap.c is where it lives. A help list built from that table cannot
			 * describe a key the program does not answer.
			 */
			if (keymap_hit(VNG_ACT_OVERLAY, &e))        { overlay = !overlay;   break; }
			if (keymap_hit(VNG_ACT_PANEL_THUMB, &e))    { thumb_toggle();       break; }
			if (keymap_hit(VNG_ACT_PANEL_COLOUR, &e))   { colour_toggle();      break; }
			if (keymap_hit(VNG_ACT_PANEL_PALETTE, &e))  { palette_toggle();     break; }
			if (keymap_hit(VNG_ACT_PANEL_ANIM, &e))     { anim_toggle();        break; }
			if (keymap_hit(VNG_ACT_PANEL_SHEETS, &e))   { tabbar_toggle();      break; }
			if (keymap_hit(VNG_ACT_PANEL_PROJECTS, &e)) { sidebar_toggle();     break; }

			/*
			 * THE THREE UNDER THE LEFT HAND. 1 and 2 walk the row of sheets and 0 puts one
			 * back, and they are together because they are the same kind of gesture: getting
			 * your bearings, not editing.
			 *
			 * They replaced CTRL+TAB and CTRL+SHIFT+TAB as the way anybody would actually do
			 * it - going BACK a sheet took three keys, one of them a modifier that exists
			 * only to reverse the other. Two adjacent keys say the direction by where they
			 * are, and cost nothing to hold.
			 */
			if (keymap_hit(VNG_ACT_SHEET_PREV, &e))     { vng_tab_step(-1);     break; }
			if (keymap_hit(VNG_ACT_SHEET_NEXT, &e))     { vng_tab_step(+1);     break; }
			if (keymap_hit(VNG_ACT_VIEW_HOME, &e))      { view_home(vng_tab);   break; }

			if (e.key.mod & SDL_KMOD_CTRL) {
				switch (e.key.key) {
				case SDLK_N: new_sheet_ask();                   break;
				case SDLK_W: file_close_tab(vng_tab);           break;
				case SDLK_O: file_open_ask();                   break;

				/* CTRL+Z back, CTRL+SHIFT+Z or CTRL+Y forward. Both redo spellings,
				 * because half the world learned one and half the other. */
				case SDLK_Z:
					if (e.key.mod & SDL_KMOD_SHIFT) undo_redo(vng_tab);
					else                            undo_undo(vng_tab);
					break;
				case SDLK_Y: undo_redo(vng_tab);                break;

				/* CTRL+S writes, CTRL+SHIFT+S always asks where. The standard pair,
				 * and the ask is the system's own dialog - see file.c. */
				case SDLK_S:
					if (e.key.mod & SDL_KMOD_SHIFT) file_save_as(vng_tab);
					else                            file_save(vng_tab);
					break;
				/* CTRL+TAB walks forward, with SHIFT it walks back. Not the arrow
				 * keys: those belong to drawing, and a key has one owner. */
				case SDLK_TAB:
					vng_tab_step((e.key.mod & SDL_KMOD_SHIFT) ? -1 : +1);
					break;
				default: break;
				}
			}
			break;

		default:
			break;
		}
	}
}

static void draw_sheet (VNG_TAB *t)
{
	if (t->tex_dirty) {
		SDL_UpdateTexture(t->tex, NULL, t->pixels, t->w * (int)sizeof(Uint32));
		t->tex_dirty = false;
	}

	SDL_FRect dst = view_sheet_rect(t);
	float     z   = t->zoom;

	/* Nearest when magnifying (pixel art has to come out square); linear when
	 * shrinking, because nearest on a downscaled photo throws away whole rows and
	 * aliases everything. */
	SDL_SetTextureScaleMode(t->tex, z >= 1.0f ? SDL_SCALEMODE_NEAREST
	                                          : SDL_SCALEMODE_LINEAR);

	/* NOTHING is drawn under the sheet. What shows through a transparent pixel is the
	 * checkerboard, which is the whole point of having one - white paper underneath
	 * would make an empty image and a white image look identical. */
	SDL_RenderTexture(vng_ren, t->tex, NULL, &dst);

	/*
	 * The stroke in progress, OVER the document and not yet in it. That separation is
	 * what lets a stroke be abandoned, and what stops a half transparent brush from
	 * blending onto its own output - see the mask in tabs.h.
	 *
	 * Only the rectangle the stroke has reached goes to the GPU. The preview is
	 * transparent everywhere else and stays that way, which is the invariant
	 * vng_tab_stroke_close maintains on the way out.
	 */
	if (t->stroke && t->tex_preview && t->sx1 > t->sx0 && t->sy1 > t->sy0) {
		SDL_Rect r = { t->sx0, t->sy0, t->sx1 - t->sx0, t->sy1 - t->sy0 };
		SDL_UpdateTexture(t->tex_preview, &r,
		                  t->pixels_preview + (size_t)r.y * t->w + r.x,
		                  t->w * (int)sizeof(Uint32));

		SDL_SetTextureScaleMode(t->tex_preview, z >= 1.0f ? SDL_SCALEMODE_NEAREST
		                                                  : SDL_SCALEMODE_LINEAR);
		SDL_RenderTexture(vng_ren, t->tex_preview, NULL, &dst);
	}

	/* A black frame just OUTSIDE the sheet, never on it. An image that is mostly alpha
	 * has no visible edge of its own, and its bounds are exactly what a person needs to
	 * see while resizing or drawing near the border. Outside by one pixel so it never
	 * hides the outermost row of the artwork - the same rule the corner grips follow. */
	SDL_FRect edge = { dst.x - 1.0f, dst.y - 1.0f, dst.w + 2.0f, dst.h + 2.0f };
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xFF);
	SDL_RenderRect(vng_ren, &edge);

}

/*
 * THE ZOOM, BOTTOM RIGHT, AND IT IS A READOUT RATHER THAN CHROME.
 *
 * The distinction is the one already made for the two loaded colours at the other end of the
 * same line: a toolbar is COMMANDS parked on screen in case they are wanted, and a readout
 * answers a question about the state of the thing in your hand. "Which colour lands if I press
 * the left button" is one of those; "what scale am I looking at" is the other, and it is the
 * question a person asks every time they come back to a drawing after doing something else.
 * There is no other way to know it - the sheet fills the window, so nothing on screen says
 * whether that is 200% or 800%.
 *
 * BOTTOM RIGHT BECAUSE THE COLOURS ARE BOTTOM LEFT. Same margin, same bar height, so the two
 * read as the ends of one line across the foot of the window rather than as two decisions.
 *
 * IT SAYS THE SAME THING F1 SAYS, in the same units, on purpose - which is why both go through
 * vangopix_zoom_text below. Two readouts of one number in different units is a small trap that costs
 * somebody an afternoon exactly once.
 */
#define ZOOM_MARGIN  (ui_pad() * 2.0f)   /* SLOT_MARGIN in tool.c, the other end of this line */
#define ZOOM_PAD     ui_pad()

/*
 * THE ZOOM AS A MULTIPLIER, WHICH IS THE UNIT THIS PROGRAM ALREADY THINKS IN. Every note about
 * the camera is written that way - "at 3x one art pixel is a 3x3 square", "1:1 where one art
 * pixel is one screen pixel" - and it is the question actually being asked: how many screen
 * pixels is one of mine.
 *
 * Every step at or above 1:1 is an integer by design, so those come out clean - 1x, 8x, 64x.
 * Below it the ladder is halves and it says so, 0.5x and 0.125x; and view_reset can land
 * BETWEEN steps entirely, since the fit of an odd-sized image is whatever it is, so 4.25x has
 * to be sayable too.
 *
 * The trailing zeros are trimmed by hand rather than with %g, which SDL_snprintf is under no
 * obligation to implement: only %d and %f are asked of it here.
 */
void vangopix_zoom_text (float z, char *dst, size_t cap)
{
	if (z == SDL_floorf(z)) {
		SDL_snprintf(dst, cap, "%dx", (int)z);
		return;
	}

	SDL_snprintf(dst, cap, "%.4f", z);

	size_t n = SDL_strlen(dst);
	while (n > 1 && dst[n - 1] == '0') dst[--n] = 0;
	if    (n > 1 && dst[n - 1] == '.') dst[--n] = 0;

	SDL_snprintf(dst + n, cap - n, "x");
}

/*
 * ONE READOUT BAR, AND THERE ARE NOW THREE THINGS ON THAT LINE.
 *
 * The two loaded colours at the left, the document's size beside them, and the zoom at the
 * right - all the same panel with a different value in it, so they are one function. Written
 * twice they would drift the first time either the face or the padding moved, which is the
 * mistake the four close boxes had already made.
 *
 * The HEIGHT comes from the colour bars rather than from the text, so everything on that line
 * is the same height whatever any of them happens to be showing.
 */
static float readout_w (const char *text)
{
	float tw = 0.0f;

	if (vng_text) text_measure(vng_text, text, &tw, NULL);
	return tw + ZOOM_PAD * 2.0f;
}

static void readout (float x, const char *text)
{
	float bw, bh, th = 0.0f;

	if (!vng_text) return;

	tool_bar_size(&bw, &bh);
	text_measure(vng_text, text, NULL, &th);

	SDL_FRect r = { x, (float)vng_win_h - ZOOM_MARGIN - bh, readout_w(text), bh };

	prim_fill(r, 0xF0141414u);
	prim_rect(r, 0xFF303030u);

	text_print(vng_text, r.x + ZOOM_PAD, r.y + SDL_floorf((bh - th) * 0.5f),
	           0xDCDCDCFF, "%s", text);
}

/*
 * THE DOCUMENT'S SIZE, BESIDE THE TWO COLOURS.
 *
 * The same kind of thing they are: state of the thing in your hand, not a command parked on
 * screen. "How big is this sheet" is asked constantly while drawing - it decides whether a
 * sprite will fit, what a resize did, and which of two open documents you are looking at -
 * and until now the only way to ask was F1.
 *
 * It sits at tool_slots_edge() rather than at a number of its own, so it stays put when the
 * colour bars step aside for the project panel.
 */
static void draw_size (VNG_TAB *t)
{
	char text[24];

	SDL_snprintf(text, sizeof text, "%dx%d", t->w, t->h);
	readout(tool_slots_edge() + ZOOM_PAD, text);
}

static void draw_zoom (VNG_TAB *t)
{
	char text[16];

	vangopix_zoom_text(t->zoom, text, sizeof text);
	readout((float)vng_win_w - ZOOM_MARGIN - readout_w(text), text);
}

static void draw_overlay (VNG_TAB *t, float zoom)
{
	if (!vng_text) return;

	/* The same units as the corner readout, and from the same call - see vangopix_zoom_text. */
	char z[16];
	vangopix_zoom_text(zoom, z, sizeof z);

	text_print(vng_text, 8.0f, 6.0f, 0xFFFFFFC0,
	           "%s\n%d x %d   %s   tab %d/%d",
	           t->name, t->w, t->h, z,
	           vng_tab_index(t), vng_tab_count());
}

/*
 * ---- WHAT THE KEYS ARE, WRITTEN ON THE EMPTY DESK ----
 *
 * The list itself lives in keymap.c, which owns it and the keyboard.txt beside the executable
 * that a person can edit. This is only the drawing of it.
 *
 * THE BLOCK IS CENTRED, NOT THE LINES.
 *
 * Centring each line on its own would leave every left edge in a different place and the key
 * column in none at all - a list nobody can run an eye down. So the widest WHOLE row decides
 * where a column starts, and after that every row in it begins at the same x. The key column
 * is as wide as the widest KEY, so the descriptions line up too. Two measurements, and no
 * layout system.
 *
 * IT IS CENTRED IN WHAT IS LEFT OF THE WINDOW, not in the window. The project strip is at the
 * left edge when the program starts; a block centred behind it is a block half hidden by the
 * thing that was put there to help. sidebar_edge is how far the panel reaches THIS FRAME, so
 * the list also slides over as the panel slides away.
 */

/* One column of the list: which rows, how wide its key column is, and how wide it is. */
typedef struct { int from, to; float kw, w; } KEYCOL;

static bool row_is_head (const VNG_KEYROW *r) { return r && r->key && !r->does; }

static void col_measure (KEYCOL *c, float gap)
{
	c->kw = 0.0f;
	c->w  = 0.0f;

	/* MEASURED PER COLUMN, not once for the whole list. A column of `Q W E R` beside one of
	 * `CTRL SHIFT S` would otherwise carry the wide one's key column through both, and the
	 * short side would be a stripe of nothing as wide as the words next to it. */
	for (int i = c->from; i < c->to; i++) {
		const VNG_KEYROW *r = keymap_row(i);
		if (!r || !r->key || !r->does) continue;

		float w;
		text_measure(vng_text, r->key, &w, NULL);
		if (w > c->kw) c->kw = w;
	}

	for (int i = c->from; i < c->to; i++) {
		const VNG_KEYROW *r = keymap_row(i);
		if (!r || !r->key) continue;

		float w;
		if (r->does) {
			text_measure(vng_text, r->does, &w, NULL);
			w += c->kw + gap;
		} else {
			text_measure(vng_text, r->key, &w, NULL);
		}
		if (w > c->w) c->w = w;
	}
}

static void col_draw (const KEYCOL *c, float x, float y, float line, float gap)
{
	for (int i = c->from; i < c->to; i++) {
		float ry = y + (float)(i - c->from) * line;
		if (ry > (float)vng_win_h) break;

		const VNG_KEYROW *r = keymap_row(i);
		if (!r || !r->key) continue;

		if (!r->does) {
			text_print(vng_text, x, ry, 0xFF8000FF, "%s", r->key);
			continue;
		}

		text_print(vng_text, x,               ry, 0xE6E6E6FF, "%s", r->key);
		text_print(vng_text, x + c->kw + gap, ry, 0x8C8C8CFF, "%s", r->does);
	}
}

/*
 * WHERE TO BREAK THE LIST IN TWO, and it is not halfway.
 *
 * Halfway by row count lands wherever it lands - in the middle of `tools`, with four of the
 * nine in one column and five in the other under no heading at all. A reader hunting for the
 * eraser then has to know the list is continuous across a gap that looks like a boundary.
 *
 * So the break happens at a HEADING, and the search walks outward from the middle to find the
 * nearest one: the columns come out uneven, and that is the cheaper cost by far. A blank line
 * would be left stranded at the top of the second column, so the heading itself starts it.
 *
 * Returns `lot` when there is no heading to break at - one column, uneven or not.
 */
static int col_break (int lot)
{
	int mid = (lot + 1) / 2;

	for (int step = 0; step < lot; step++) {
		int up = mid + step, down = mid - step;

		if (up   < lot && row_is_head(keymap_row(up)))   return up;
		if (down > 0   && row_is_head(keymap_row(down))) return down;
	}
	return lot;
}

static void draw_keys (void)
{
	if (!vng_text) return;

	float line = ui_line() + ui_pad();
	float gap  = ui_cell() * 2.0f;
	float pad  = ui_pad() * 2.0f;

	int lot = keymap_lot();
	if (lot < 1) return;

	/*
	 * TWO COLUMNS ONLY WHEN ONE WILL NOT FIT.
	 *
	 * The number of columns follows the room rather than being decided in advance: on a tall
	 * window one column is the easier thing to read, and on a short one the list was being cut
	 * off at the bottom with nothing to say that it went on.
	 */
	int room_rows = (int)(((float)vng_win_h - pad * 2.0f) / line);

	KEYCOL col[2];
	int    lots = 1;

	col[0].from = 0;
	col[0].to   = lot;

	if (lot > room_rows) {
		int cut = col_break(lot);

		if (cut > 0 && cut < lot) {
			col[0].to   = cut;
			col[1].from = cut;
			col[1].to   = lot;
			lots = 2;
		}
	}

	for (int i = 0; i < lots; i++) col_measure(&col[i], gap);

	float wide = col[0].w + (lots > 1 ? gap * 2.0f + col[1].w : 0.0f);

	int tall = col[0].to - col[0].from;
	if (lots > 1 && col[1].to - col[1].from > tall) tall = col[1].to - col[1].from;

	float edge = sidebar_edge();

	float x = edge + SDL_floorf(((float)vng_win_w - edge - wide) * 0.5f);
	float y = SDL_floorf(((float)vng_win_h - (float)tall * line) * 0.5f);

	/* A window too short even for the columns shows the TOP of them rather than the middle:
	 * the first rows are the ones somebody arriving needs, and a list centred out of both
	 * edges at once shows neither end. */
	if (y < pad)        y = pad;
	if (x < edge + pad) x = edge + pad;

	/* BOTH COLUMNS START ON THE SAME LINE. Hanging the shorter one from the middle would put
	 * its heading somewhere no eye expects a list to begin. */
	col_draw(&col[0], x, y, line, gap);

	if (lots > 1)
		col_draw(&col[1], x + col[0].w + gap * 2.0f, y, line, gap);
}

/* The frame clock. It lives here because the frame does, and it is handed to the rest of
 * the program as vng_dt so that nothing else has to start a clock of its own.
 *
 * The first frame reports zero rather than the whole time init took, and any frame longer
 * than a tenth of a second is clamped: dragging or resizing the window blocks this loop
 * on Windows for as long as the hand holds it, and an animation fed that gap would jump
 * to its end instead of resuming. */
#define DT_MAX 0.1f

static void frame_clock (void)
{
	static Uint64 last = 0;

	Uint64 now = SDL_GetTicksNS();
	vng_dt = last ? (float)((double)(now - last) / 1e9) : 0.0f;
	last   = now;

	if (vng_dt > DT_MAX) vng_dt = DT_MAX;
}

void vangopix_core (void)
{
	frame_clock();
	SDL_GetWindowSize(vng_win, &vng_win_w, &vng_win_h);

	SDL_SetRenderDrawColor(vng_ren, 0x25, 0x25, 0x25, 0xFF);
	SDL_RenderClear(vng_ren);
	draw_desk();

	VNG_TAB *t = vng_tab;
	if (t) {
		/*
		 * F1 PUTS THE KEYS BACK, BEHIND THE ARTWORK.
		 *
		 * The empty desk teaches the program once, and then the first sheet covers it for
		 * good - which is fine for the twenty keys somebody has learned and no use at all for
		 * the twentieth they have not. So the same list comes back under F1.
		 *
		 * BEHIND the sheet, drawn before it, and that is the whole reason this is up here
		 * rather than beside draw_overlay at the bottom of the block: a reminder that covers
		 * the drawing is a reminder you have to dismiss before you can act on it. Behind, the
		 * artwork stays exactly where it was and the list fills the desk around it.
		 */
		if (overlay) draw_keys();

		/* Before the sheet is drawn, because what the spray lays down this frame has to
		 * reach the texture in the same frame. */
		anim_tick();       /* the playhead, on the program clock */
		tool_frame(t);

		draw_sheet(t);
		select_draw(t);    /* the float and its marching rectangle, over the sheet */
		thumb_draw(t);     /* the marker on the sheet; win_draw draws the panel itself */
		anim_draw(t);      /* the clip grid on the sheet, the same split thumb makes */
		tool_draw(t);      /* the tip outline and the glyph, under the panels */
		draw_size(t);      /* beside the two colours: how big the sheet is */
		draw_zoom(t);      /* and the other end of the same line */
		resize_draw(t);
		if (overlay) draw_overlay(t, t->zoom);
		sidebar_draw();
		palette_grid_draw(t);   /* the swatches beside the palette box, under the windows */
		win_draw();      /* the floating windows, over the sheet and under the panels */
		tabbar_draw();   /* last of the layers, so it floats over the sheet */

		/* AND OVER EVEN THAT, because it exists only while a key is held: nothing summoned
		 * for as long as a hand keeps a key down should come up behind something parked. It
		 * matches where it sits in the chain above. */
		palette_quick_draw(t);

		/* AND THE PREVIEW UNDER THE POINTER, for the same reason and in the same place: it
		 * exists only while a hand is holding still on a name in the sidebar, and a picture
		 * that came up behind a parked window would be answering nobody. */
		sidebar_hover_draw();
	} else {
		/*
		 * NO DOCUMENT, AND THAT IS A STATE NOW. The desk is already drawn; what goes on it
		 * is the only place this program can say what it does - see draw_keys.
		 *
		 * THE PROJECT PANEL STAYS UP HERE, which is the other half of the answer: the list
		 * says which keys open a file and the panel is somewhere to open one FROM, without
		 * knowing any key at all. It is also the only thing on screen with a slide to
		 * advance, so leaving it out of this branch would freeze the panel half open the
		 * moment the last sheet was closed.
		 */
		draw_keys();
		sidebar_draw();
		sidebar_hover_draw();
	}

	/* OUTSIDE the block: the field holds the keyboard, and a keyboard captured with no
	 * caret on screen is a program that has stopped answering. It does not depend on
	 * there being a document, and it draws over everything that does. */
	prompt_draw();

	/* OVER EVERYTHING, because until it is dismissed there is nothing else on screen worth
	 * seeing - the mirror of it answering events before everything. */
	splash_draw();

	/* LAST, after everything that could have asked for a shape. One place owns the cursor
	 * because the machine has one - see tool_cursor in tool.h. */
	tool_cursor_apply();

	SDL_RenderPresent(vng_ren);
}
