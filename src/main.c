/*
 * Vangopix - a pixel art editor.
 *
 * main.c = the frame envelope: window, loop, present. The logic lives in the modules.
 *
 * THE MINIMALISM HERE IS IN THE INTERFACE, NOT IN THE FEATURES: no docked panel, no
 * permanent bar, not one screen pixel reserved for chrome. The sheet fills the window.
 * Whatever needs a window shows up on demand and goes away afterwards.
 */

#include "vangopix.h"
#include "tabs.h"

#define NEW_W  64
#define NEW_H  64
#define MARGIN 24   /* the desk around the paper: without it the white touches the
                     * window edge and the eye loses where the document ends */

SDL_Window   *vng_win;
SDL_Renderer *vng_ren;
int           vng_win_w = 800, vng_win_h = 600;
bool          vng_loop  = true;

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

static void vangopix_input (void)
{
	SDL_Event e;

	while (SDL_PollEvent(&e)) {
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

			if (e.key.mod & SDL_KMOD_CTRL) {
				switch (e.key.key) {
				case SDLK_N:   vng_tab_new(NEW_W, NEW_H);  break;
				case SDLK_W:   vng_tab_close(vng_tab);     break;
				/* CTRL+TAB walks forward, with SHIFT it walks back. Not the arrow
				 * keys: those belong to drawing, and a key has one owner. */
				case SDLK_TAB: vng_tab_step((e.key.mod & SDL_KMOD_SHIFT) ? -1 : +1); break;
				default: break;
				}
			}
			break;

		default:
			break;
		}
	}
}

static void vangopix_core (void)
{
	SDL_GetWindowSize(vng_win, &vng_win_w, &vng_win_h);

	/* The background is dark grey, not white: it is the desk under the paper. A white
	 * background would make the sheet vanish, and the sheet is the whole interface. */
	SDL_SetRenderDrawColor(vng_ren, 0x1E, 0x1E, 0x1E, 0xFF);
	SDL_RenderClear(vng_ren);

	VNG_TAB *t = vng_tab;
	if (t) {
		if (t->tex_dirty) {
			SDL_UpdateTexture(t->tex, NULL, t->pixels, t->w * (int)sizeof(Uint32));
			t->tex_dirty = false;
		}

		float z;
		SDL_FRect dst = sheet_rect(t, &z);

		/* Nearest when magnifying (pixel art has to come out square); linear when
		 * shrinking, because nearest on a downscaled photo throws away whole rows
		 * and aliases everything. */
		SDL_SetTextureScaleMode(t->tex, z >= 1.0f ? SDL_SCALEMODE_NEAREST
		                                          : SDL_SCALEMODE_LINEAR);

		/* The white paper UNDER the image: it is what gives transparency a body
		 * without inventing a checkerboard. A checkerboard is interface decoration;
		 * here the sheet is white. */
		SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xFF);
		SDL_RenderFillRect(vng_ren, &dst);
		SDL_RenderTexture(vng_ren, t->tex, NULL, &dst);
	}

	SDL_RenderPresent(vng_ren);
}

int main (int argc, char **argv)
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("SDL_Init: %s", SDL_GetError());
		return 1;
	}
	if (!SDL_CreateWindowAndRenderer("Vangopix", vng_win_w, vng_win_h,
	                                 SDL_WINDOW_RESIZABLE, &vng_win, &vng_ren)) {
		SDL_Log("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
		return 1;
	}
	SDL_SetRenderVSync(vng_ren, 1);

	/* Every file on the command line becomes a tab. This is what makes it possible to
	 * associate Vangopix with an extension on Windows and open a selection at once. */
	for (int i = 1; i < argc; i++)
		vng_tab_open(argv[i]);

	/* Nothing opened (or no argument came): the program always has paper. */
	if (!vng_tab)
		vng_tab_new(NEW_W, NEW_H);

	while (vng_loop) {
		vangopix_input();
		vangopix_core();
	}

	vng_tabs_free();
	SDL_DestroyRenderer(vng_ren);
	SDL_DestroyWindow(vng_win);
	SDL_Quit();
	return 0;
}
