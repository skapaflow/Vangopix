/*
 * Vangopix - a pixel art editor.
 *
 * The name is Van Gogh plus pixel.
 *
 * main.c is the pipeline and nothing else: bring the program up, run the frame until
 * it is asked to stop, take it down. Every line here is a call, so the shape of the
 * program is readable in one screen and no decision hides in the entry point.
 *
 *   vangopix.c  the program: globals, window, renderer, font, argv
 *   core.c      the frame: input, draw, present
 *   tabs.c      the tabs, which ARE the documents
 *   text.c      glyphs out of one atlas
 *
 * THE MINIMALISM IS A GUIDELINE, NOT A LAW: it settles a question when nothing else
 * does. What it rules out is permanent chrome - a docked panel, a toolbar, a strip of
 * screen spent whether it is wanted or not. What it never rules out is a feature.
 */

/* SDL_main.h belongs in exactly one translation unit, and this is it. On Windows it
 * supplies the WinMain the linker wants once -mwindows drops the console; on Linux and
 * macOS it resolves to nothing. Including it here is what lets the same main() serve
 * all three. */
#include <SDL3/SDL_main.h>

#include "vangopix.h"
#include "core.h"

int main (int argc, char **argv)
{
	if (!vangopix_init(argc, argv))
		return 1;

	while (vng_loop) {
		vangopix_input();
		vangopix_core();
	}

	vangopix_quit();
	return 0;
}
