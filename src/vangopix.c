#include "vangopix.h"
#include "tabs.h"
#include "core.h"
#include "project.h"
#include "anim.h"
#include "sidebar.h"
#include "keymap.h"
#include "splash.h"
#include "file.h"
#include "tool.h"
#include "select.h"
#include "win.h"
#include "colour.h"

SDL_Window   *vng_win   = NULL;
SDL_Renderer *vng_ren   = NULL;
TextSystem   *vng_text  = NULL;
TextSystem   *vng_text_small = NULL;
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
/*
 * TWENTY, AND IT MOVED FROM SIXTEEN ONLY ONCE THE LAYOUT FOLLOWED IT.
 *
 * Every window in this program was ported from the first Vangopix, which drew text with a 6x6
 * BITMAP font: its list rows, buttons and form fields were sized around text six to eighteen
 * pixels high. Carried over literally, they were holding a sixteen pixel line - which is why
 * so much of it read as cramped, and why one button was thirteen pixels tall for text that
 * measured sixteen.
 *
 * Raising this before ui.h existed would have made that worse rather than better: the boxes
 * were fixed and the text inside them would simply have overflowed. Now every rectangle that
 * holds text is stated as a multiple of ui_line(), ui_row() or ui_cell(), so this one number
 * moves all of them together. Anything holding PIXELS - the palette's swatch cell, the colour
 * discs, the wheel - is deliberately untouched by it. See ui.h.
 */
#define VNG_FONT_SIZE 20.0f

/*
 * The small face, for a label in a box that the main one would fill - the palette's COLOR
 * button, the animation window's row of words, a form's field names.
 *
 * SEVEN TENTHS OF THE MAIN FACE, which is the ratio that was already working at 11 against 16.
 * It is a ratio and not a second constant on purpose: two independent sizes drift, and the
 * whole point of the number above is that one knob moves the interface.
 */
#define VNG_FONT_SMALL 14.0f

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

bool vangopix_read_line (SDL_IOStream *io, char *dst, size_t cap)
{
	size_t n = 0;
	char   ch;

	if (!io || !dst || cap == 0) return false;

	while (SDL_ReadIO(io, &ch, 1) == 1) {
		if (ch == '\n') { dst[n] = 0; return true; }
		if (ch != '\r' && n + 1 < cap) dst[n++] = ch;
	}
	dst[n] = 0;
	return n > 0;   /* a last line with no newline is still a line */
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

	/*
	 * The font is not fatal. Nothing the editor does to an image depends on being able to draw
	 * a letter, so a missing or unreadable face costs the overlay and nothing else -
	 * text_draw on a NULL system is a no-op by design.
	 *
	 * WHICH FACE LOADED IS NOT REPORTED. Nothing is decided by it and nobody is waiting to
	 * hear it, and a line printed on every successful start is the kind of noise that trains
	 * a person to stop reading the output - which is the whole value of the lines below, all
	 * of which report a FAILURE.
	 */
	for (size_t i = 0; i < SDL_arraysize(vng_fonts) && !vng_text; i++) {
		char *font = vangopix_asset(vng_fonts[i]);
		if (!font) continue;
		vng_text = text_init(vng_ren, font, VNG_FONT_SIZE);
		SDL_free(font);
	}
	if (!vng_text)
		SDL_Log("running without text: no font found beside the executable");

	/* The same face again, packed smaller - see vng_text_small in vangopix.h for why this is
	 * a second atlas rather than a scale factor. It is not reported when it fails either:
	 * falling back to the main face costs a label that is bigger than it wanted to be, and
	 * nothing else. */
	for (size_t i = 0; i < SDL_arraysize(vng_fonts) && vng_text && !vng_text_small; i++) {
		char *font = vangopix_asset(vng_fonts[i]);
		if (!font) continue;
		vng_text_small = text_init(vng_ren, font, VNG_FONT_SMALL);
		SDL_free(font);
	}
	if (!vng_text_small) vng_text_small = vng_text;

	tool_init();   /* not fatal either: without the cursors the pointer keeps whatever
	                * shape the system gave it, and drawing works the same */

	file_init();   /* not fatal: without it save-as cannot deliver its answer, and the
	                * log says so - everything else in the program still works */

	project_load();

	/* The list on the empty desk, and the keyboard.txt it is read from - written from the
	 * built-in defaults when that file is not there. Not fatal: without it the desk shows the
	 * same list from memory. */
	keymap_load();

	/* Every path on the command line becomes a tab. This is what lets Vangopix be
	 * associated with an image extension and handed a whole selection at once. */
	for (int i = 1; i < argc; i++)
		vng_tab_open(argv[i]);

	/*
	 * AND IF NOTHING WAS ASKED FOR, NOTHING IS OPENED.
	 *
	 * It used to make a blank sheet here so the program always had paper. An untitled sheet
	 * nobody asked for is a decision taken on somebody's behalf, and it is in the way of the
	 * two things a person actually arrives to do: drop a file in, or say what size they want.
	 * The desk is left bare and core.c writes the keys across it, which is the only teaching
	 * this program has room for - it has no menus to read.
	 */
	/* The picture the program opens with. After the font, because its words are drawn INTO
	 * it - see splash.h. */
	splash_open();

	/*
	 * THE PROJECT PANEL IS NOT RAISED HERE, and that is deliberate rather than left out.
	 *
	 * It offers itself instead: with no document, sidebar.c shows a narrow strip at the left
	 * edge that slides the panel out when it is pressed - see sidebar.h. Opening it the whole
	 * way on somebody's behalf would put a quarter of the window in front of the page that
	 * explains the program, to answer a question they may not have.
	 *
	 * The strip is enough. It is the only part of this interface that can be used by pointing,
	 * which is all somebody arriving needs to have.
	 */
	return true;
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
	/* Only once when the small face never loaded and is pointing at the main one. */
	if (vng_text_small != vng_text) text_free(vng_text_small);
	vng_text_small = NULL;

	anim_free();
	sidebar_free();
	splash_free();

	text_free(vng_text);
	if (vng_ren) SDL_DestroyRenderer(vng_ren);
	if (vng_win) SDL_DestroyWindow(vng_win);
	SDL_Quit();
}
