#include "core.h"
#include "vangopix.h"
#include "tabs.h"
#include "tabbar.h"

/* The desk around the paper: without it the white touches the window edge and the eye
 * loses where the document ends. */
#define MARGIN 24

/* The overlay is OFF by default and it is not chrome: it occupies no space when it is
 * not asked for, and it is the only consumer of the text module today. It exists so
 * that module is proven rather than merely compiled. F1 toggles it; deleting it is
 * deleting one call in vangopix_core and one case in vangopix_input. */
static bool overlay = false;

/*
 * Where the sheet lands on screen.
 *
 * Two rules, not one. When the sheet FITS, zoom is an integer: one art pixel has to
 * become an exact N by N square, otherwise nearest rounds differently on each column
 * and the columns come out unequal in width - the flaw that gives away a badly made
 * editor. When it does NOT fit (a 4000px photo), integer zoom would be 0; there the
 * fractional reduction is right, because seeing the whole image matters more than a
 * perfect grid.
 */
static SDL_FRect sheet_rect (VNG_TAB *t, float *zoom_out)
{
	float aw = (float)(vng_win_w - 2 * MARGIN);
	float ah = (float)(vng_win_h - 2 * MARGIN);
	if (aw < 1) aw = 1;
	if (ah < 1) ah = 1;

	float fit = SDL_min(aw / t->w, ah / t->h);
	float z   = fit >= 1.0f ? SDL_floorf(fit) : fit;

	float w = t->w * z, h = t->h * z;
	/* Centred on integer coordinates: half a pixel of offset brings back the same
	 * uneven rounding the integer zoom just avoided. */
	SDL_FRect r = { SDL_floorf((vng_win_w - w) / 2.0f),
	                SDL_floorf((vng_win_h - h) / 2.0f), w, h };
	*zoom_out = z;
	return r;
}

void vangopix_input (void)
{
	SDL_Event e;

	while (SDL_PollEvent(&e)) {

		/* The bar gets first refusal while it is up. It floats OVER the sheet, so
		 * without this a click meant for a tab would also land on the drawing
		 * underneath - and once tools exist, that is a stray pixel every time. */
		if (tabbar_event(&e))
			continue;

		switch (e.type) {

		case SDL_EVENT_QUIT:
			vng_loop = false;
			break;

		/* Dropping a file opens it in a new tab - the gesture that lets Vangopix act
		 * as the machine's image viewer without owning a File menu. */
		case SDL_EVENT_DROP_FILE:
			vng_tab_open(e.drop.data);
			break;

		case SDL_EVENT_KEY_DOWN:
			if (e.key.repeat) break;

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
				default: break;
				}
			}
			break;

		default:
			break;
		}
	}
}

static void draw_sheet (VNG_TAB *t, float *zoom_out)
{
	if (t->tex_dirty) {
		SDL_UpdateTexture(t->tex, NULL, t->pixels, t->w * (int)sizeof(Uint32));
		t->tex_dirty = false;
	}

	float z;
	SDL_FRect dst = sheet_rect(t, &z);

	/* Nearest when magnifying (pixel art has to come out square); linear when
	 * shrinking, because nearest on a downscaled photo throws away whole rows and
	 * aliases everything. */
	SDL_SetTextureScaleMode(t->tex, z >= 1.0f ? SDL_SCALEMODE_NEAREST
	                                          : SDL_SCALEMODE_LINEAR);

	/* The white paper UNDER the image: it is what gives transparency a body without
	 * inventing a checkerboard. A checkerboard is interface decoration; here the
	 * sheet is white. */
	SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xFF);
	SDL_RenderFillRect(vng_ren, &dst);
	SDL_RenderTexture(vng_ren, t->tex, NULL, &dst);

	*zoom_out = z;
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

	/* The background is dark grey, not white: it is the desk under the paper. A white
	 * background would make the sheet vanish, and the sheet is the whole interface. */
	SDL_SetRenderDrawColor(vng_ren, 0x1E, 0x1E, 0x1E, 0xFF);
	SDL_RenderClear(vng_ren);

	VNG_TAB *t = vng_tab;
	if (t) {
		float zoom = 1.0f;
		draw_sheet(t, &zoom);
		if (overlay) draw_overlay(t, zoom);
		tabbar_draw();   /* last, so it floats over the sheet instead of under it */
	}

	SDL_RenderPresent(vng_ren);
}
