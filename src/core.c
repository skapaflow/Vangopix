#include "core.h"
#include "vangopix.h"
#include "tabs.h"
#include "tabbar.h"
#include "view.h"
#include "resize.h"
#include "sidebar.h"
#include "project.h"

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

/* CTRL+ALT tapped together, with nothing else pressed in between, toggles the sidebar.
 *
 * A shortcut made only of modifiers cannot be recognised on the way DOWN - at that
 * moment it is indistinguishable from the start of CTRL+ALT+something. So it is
 * recognised on the way up: arm when both are held, disarm the moment any other key or
 * any mouse button is used, and fire when the first of the two is released while still
 * armed. */
static bool chord = false;

static bool is_mod_key (SDL_Keycode k)
{
	return k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LALT || k == SDLK_RALT;
}

void vangopix_input (void)
{
	SDL_Event e;

	while (SDL_PollEvent(&e)) {

		/* The bar gets first refusal while it is up. It floats OVER the sheet, so
		 * without this a click meant for a tab would also land on the drawing
		 * underneath - and once tools exist, that is a stray pixel every time. */
		/* Any mouse button disarms the chord: CTRL+ALT with a click is a click. */
		if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
			chord = false;

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

		case SDL_EVENT_KEY_UP:
			if (is_mod_key(e.key.key) && chord) {
				chord = false;
				sidebar_toggle();
			}
			break;

		case SDL_EVENT_KEY_DOWN:
			if (e.key.repeat) break;

			if (is_mod_key(e.key.key)) {
				SDL_Keymod m = SDL_GetModState();
				if ((m & SDL_KMOD_CTRL) && (m & SDL_KMOD_ALT))
					chord = true;
				break;
			}
			chord = false;   /* the modifiers are being used for something else */

			if (e.key.key == SDLK_F1) {
				overlay = !overlay;
				break;
			}

			/* Bare TAB shows and hides the bar; CTRL+TAB below still walks between
			 * documents. One key, two jobs, told apart by the modifier - and the
			 * word is the same in both languages. */
			if (e.key.key == SDLK_TAB && !(e.key.mod & SDL_KMOD_CTRL)) {
				tabbar_toggle();
				break;
			}

			if (e.key.mod & SDL_KMOD_CTRL) {
				switch (e.key.key) {
				case SDLK_N: vng_tab_new(VNG_NEW_W, VNG_NEW_H); break;
				case SDLK_W: vng_tab_close(vng_tab);            break;
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

void vangopix_core (void)
{
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
		tabbar_draw();   /* last, so it floats over the sheet instead of under it */
	}

	SDL_RenderPresent(vng_ren);
}
