#ifndef VANGOPIX_H
#define VANGOPIX_H

/*
 * The objects every module needs to name, and the two calls that bracket the program.
 * No per-frame logic lives here - that is core.c.
 */

#include <stdbool.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "text.h"

#define VNG_NAME    "Vangopix"
#define VNG_WIN_W   800
#define VNG_WIN_H   600
#define VNG_NEW_W   64      /* a fresh sheet, in document pixels */
#define VNG_NEW_H   64

extern SDL_Window   *vng_win;
extern SDL_Renderer *vng_ren;
extern TextSystem   *vng_text;
extern int           vng_win_w, vng_win_h;
extern bool          vng_loop;

/* Brings up SDL, the window, the renderer and the font, then turns every path in argv
   into a tab. False means nothing usable came up and the program should stop. */
extern bool vangopix_init (int argc, char **argv);

/* Tears down in the reverse order. Safe to call after a partial init. */
extern void vangopix_quit (void);

/* Builds a path to a file shipped beside the executable, whatever the platform and
   whatever directory the program was launched from. Free the result with SDL_free. */
extern char *vangopix_asset (const char *relative);

#endif
