#include "core.h"
#include "vangopix.h"
#include "tabs.h"
#include "tabbar.h"
#include "view.h"
#include "resize.h"
#include "sidebar.h"
#include "project.h"
#include "keys.h"
#include "prompt.h"
#include "file.h"

/*
 * The desk: a grey checkerboard, the size and the two greys taken from what the first
 * Vangopix had settled on in its config.ini.
 *
 * It is drawn in SCREEN space, not document space - it is the table the paper lies on,
 * so it must not slide when the camera pans, or it would read as part of the artwork.
 */
#define CHECK       6
/* 0xAARRGGBB, to match the ARGB8888 the texture is created with. The first Vangopix
 * wrote these as 0xRRGGBBAA in its config.ini, and transcribing that shape straight into
 * an RGBA32 texture is how this first came out red: RGBA32 orders the BYTES R,G,B,A, so
 * a little endian machine reads 0x252525FF back as red 0xFF with alpha 0x25. */
#define CHECK_A     0xFF252525
#define CHECK_B     0xFF303030

static SDL_Texture *checker = NULL;

/* One tile of 2x2 squares, uploaded once and repeated by the GPU.
 *
 * The obvious way - a filled rect per square - is about thirteen thousand draw calls a
 * frame at 800x600, every frame, for a backdrop that never changes. One tiled texture is
 * a single call, and it stays a single call at any window size. */
static bool checker_make (void)
{
	const int n = CHECK * 2;
	Uint32 px[CHECK * 2 * CHECK * 2];

	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
			px[y * n + x] = ((x < CHECK) == (y < CHECK)) ? CHECK_A : CHECK_B;

	checker = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
	                            SDL_TEXTUREACCESS_STATIC, n, n);
	if (!checker) {
		SDL_Log("checker: %s", SDL_GetError());
		return false;
	}
	/* Nearest, or the seam between two tiles blurs into a grey line at the joins. */
	SDL_SetTextureScaleMode(checker, SDL_SCALEMODE_NEAREST);
	SDL_UpdateTexture(checker, NULL, px, n * (int)sizeof(Uint32));
	return true;
}

static void draw_desk (void)
{
	if (!checker && !checker_make()) {
		SDL_SetRenderDrawColor(vng_ren, 0x25, 0x25, 0x25, 0xFF);
		SDL_RenderClear(vng_ren);
		return;
	}
	SDL_FRect all = { 0.0f, 0.0f, (float)vng_win_w, (float)vng_win_h };
	SDL_RenderTextureTiled(vng_ren, checker, NULL, 1.0f, &all);
}

void vangopix_core_free (void)
{
	if (checker) SDL_DestroyTexture(checker);
	checker = NULL;
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

		/* Then the canvas grips, BEFORE the camera: they answer the left button, and
		 * so does the space-pan. Whichever runs first wins the drag, and grabbing a
		 * corner has to mean resizing it. */
		if (resize_event(&e, vng_tab))
			continue;

		/* Then the camera. It answers the wheel and the pan drag; anything it does not
		 * want falls through to the keys below, and one day to the drawing tools. */
		if (view_event(&e, vng_tab))
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

			if (e.key.key == SDLK_F1) {
				overlay = !overlay;
				break;
			}

			if (e.key.key == SDLK_ESCAPE) {
				tabbar_toggle();
				break;
			}

			/* Bare TAB raises the sidebar; CTRL+TAB below still walks the documents. */
			if (e.key.key == SDLK_TAB && !(e.key.mod & SDL_KMOD_CTRL)) {
				sidebar_toggle();
				break;
			}

			if (e.key.mod & SDL_KMOD_CTRL) {
				switch (e.key.key) {
				case SDLK_N: new_sheet_ask();                   break;
				case SDLK_W: file_close_tab(vng_tab);           break;
				case SDLK_O: file_open_ask();                   break;

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
				/* The two framings every editor has: fit the whole sheet, and go to
				 * 1:1 where one art pixel is one screen pixel. */
				case SDLK_0: view_reset(vng_tab);       break;
				case SDLK_1: view_actual_size(vng_tab); break;
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

	/* A black frame just OUTSIDE the sheet, never on it. An image that is mostly alpha
	 * has no visible edge of its own, and its bounds are exactly what a person needs to
	 * see while resizing or drawing near the border. Outside by one pixel so it never
	 * hides the outermost row of the artwork - the same rule the corner grips follow. */
	SDL_FRect edge = { dst.x - 1.0f, dst.y - 1.0f, dst.w + 2.0f, dst.h + 2.0f };
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xFF);
	SDL_RenderRect(vng_ren, &edge);

}

static void draw_overlay (VNG_TAB *t, float zoom)
{
	if (!vng_text) return;

	text_print(vng_text, 8.0f, 6.0f, 0xFFFFFFC0,
	           "%s\n%d x %d   %.0f%%   tab %d/%d",
	           t->name, t->w, t->h, zoom * 100.0f,
	           vng_tab_index(t), vng_tab_count());
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
		draw_sheet(t);
		resize_draw(t);
		if (overlay) draw_overlay(t, t->zoom);
		sidebar_draw();
		tabbar_draw();   /* last of the layers, so it floats over the sheet */
	}

	/* OUTSIDE the block: the field holds the keyboard, and a keyboard captured with no
	 * caret on screen is a program that has stopped answering. It does not depend on
	 * there being a document, and it draws over everything that does. */
	prompt_draw();

	SDL_RenderPresent(vng_ren);
}
