#include "vangopix.h"
#include "tabs.h"
#include "core.h"

SDL_Window   *vng_win   = NULL;
SDL_Renderer *vng_ren   = NULL;
TextSystem   *vng_text  = NULL;
int           vng_win_w = VNG_WIN_W;
int           vng_win_h = VNG_WIN_H;
bool          vng_loop  = true;

/* Fonts are looked up beside the executable, never relative to the working directory:
 * a file manager, a desktop shortcut and a terminal each launch the program from a
 * different place, and only the executable's own directory is the same in all three.
 * SDL_GetBasePath is what makes that identical on Windows, Linux and macOS.
 *
 * THE ORDER IS A LICENSING DECISION AS MUCH AS A TYPOGRAPHIC ONE.
 *
 * Lucida Console is monospaced, which is what VagrantUI needs - it asks for one fixed
 * char_w/char_h and lays its content out in columns from it, so a proportional face
 * makes every column drift. But it is Bigelow & Holmes, shipped with Windows, and is
 * NOT redistributable: it is git-ignored, present only on machines that already have
 * it. FreeSans is the committed fallback so that a fresh clone runs; it is GNU
 * FreeFont, and it is proportional, so it is right for an overlay and wrong for
 * VagrantUI. Replacing it with a permissively licensed monospaced face (SIL OFL:
 * DejaVu Sans Mono, JetBrains Mono, Liberation Mono) closes both gaps at once, and is
 * a one-line change here. */
static const char *const vng_fonts[] = {
	"font/lucon.ttf",     /* monospaced, local only  */
	"font/FreeSans.ttf",  /* proportional, committed */
};
#define VNG_FONT_SIZE 16.0f

char *vangopix_asset (const char *relative)
{
	const char *base = SDL_GetBasePath();   /* owned by SDL; must not be freed */
	if (!base) base = "./";

	size_t n = SDL_strlen(base) + SDL_strlen(relative) + 1;
	char  *p = (char *) SDL_malloc(n);
	if (!p) return NULL;

	SDL_snprintf(p, n, "%s%s", base, relative);
	return p;
}

bool vangopix_init (int argc, char **argv)
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("SDL_Init: %s", SDL_GetError());
		return false;
	}

	if (!SDL_CreateWindowAndRenderer(VNG_NAME, vng_win_w, vng_win_h,
	                                 SDL_WINDOW_RESIZABLE, &vng_win, &vng_ren)) {
		SDL_Log("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
		return false;
	}
	SDL_SetRenderVSync(vng_ren, 1);

	/* The font is not fatal. Nothing the editor does to an image depends on being able
	 * to draw a letter, so a missing or unreadable face costs the overlay and nothing
	 * else - text_draw on a NULL system is a no-op by design. */
	for (size_t i = 0; i < SDL_arraysize(vng_fonts) && !vng_text; i++) {
		char *font = vangopix_asset(vng_fonts[i]);
		if (!font) continue;
		vng_text = text_init(vng_ren, font, VNG_FONT_SIZE);
		if (vng_text) SDL_Log("font: %s", vng_fonts[i]);
		SDL_free(font);
	}
	if (!vng_text)
		SDL_Log("running without text: no font found beside the executable");

	/* Every path on the command line becomes a tab. This is what lets Vangopix be
	 * associated with an image extension and handed a whole selection at once. */
	for (int i = 1; i < argc; i++)
		vng_tab_open(argv[i]);

	/* Nothing opened, or nothing was asked for: the program always has paper. */
	if (!vng_tab)
		vng_tab_new(VNG_NEW_W, VNG_NEW_H);

	return vng_tab != NULL;
}

void vangopix_quit (void)
{
	vng_tabs_free();
	vangopix_core_free();
	text_free(vng_text);
	if (vng_ren) SDL_DestroyRenderer(vng_ren);
	if (vng_win) SDL_DestroyWindow(vng_win);
	SDL_Quit();
}
