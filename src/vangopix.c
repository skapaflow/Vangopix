#include "vangopix.h"
#include "tabs.h"
#include "core.h"
#include "project.h"
#include "file.h"
#include "tool.h"
#include "select.h"
#include "win.h"
#include "colour.h"

SDL_Window   *vng_win   = NULL;
SDL_Renderer *vng_ren   = NULL;
TextSystem   *vng_text  = NULL;
int           vng_win_w = VNG_WIN_W;
int           vng_win_h = VNG_WIN_H;
bool          vng_loop  = true;
float         vng_dt    = 0.0f;

/* Fonts are looked up beside the executable, never relative to the working directory:
 * a file manager, a desktop shortcut and a terminal each launch the program from a
 * different place, and only the executable's own directory is the same in all three.
 * SDL_GetBasePath is what makes that identical on Windows, Linux and macOS.
 *
 * THE ORDER WAS A LICENSING PROBLEM, AND IT IS SETTLED.
 *
 * It used to ask for Lucida Console first, which is monospaced and therefore right for
 * VagrantUI - and Bigelow & Holmes property, shipped with Windows, never redistributable
 * from a public repository. FreeSans was the committed fallback so a fresh clone would
 * run at all, and it is proportional, which is wrong for a UI laid out in columns.
 *
 * DejaVu Sans Mono is both things at once: monospaced, and under the Bitstream Vera /
 * DejaVu licence, which permits redistribution outright. It is committed, with its
 * licence text beside it as that licence requires, so a clone runs with the same face
 * the author sees and text_cell() finally means what it says.
 *
 * FreeSans stays as the one behind it. Not because it is a good answer - it is
 * proportional - but because a missing or corrupt first file should cost a nicer face
 * and not the ability to read anything on screen. */
static const char *const vng_fonts[] = {
	"font/DejaVuSansMono.ttf",  /* monospaced, redistributable, committed */
	"font/FreeSans.ttf",        /* proportional; only if the first is gone */
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

	tool_init();   /* not fatal either: without the cursors the pointer keeps whatever
	                * shape the system gave it, and drawing works the same */

	file_init();   /* not fatal: without it save-as cannot deliver its answer, and the
	                * log says so - everything else in the program still works */

	project_load();

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
	tool_free();
	colour_free();
	win_free();
	select_clipboard_free();
	vng_tabs_free();
	project_free();
	vangopix_core_free();
	text_free(vng_text);
	if (vng_ren) SDL_DestroyRenderer(vng_ren);
	if (vng_win) SDL_DestroyWindow(vng_win);
	SDL_Quit();
}
