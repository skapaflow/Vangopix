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

/* The largest side a sheet may have. Not a matter of taste: the buffer is allocated as
   w * h * 4 with w and h as int, so a mistyped 640000 overflows the multiplication and
   the loop that fills it walks off an allocation that was never the size it asked for.
   8192 squared is already 256MB of ARGB, well past what pixel art is for. */
#define VNG_MAX_SIDE 8192

extern SDL_Window   *vng_win;
extern SDL_Renderer *vng_ren;
extern TextSystem   *vng_text;
extern int           vng_win_w, vng_win_h;
extern bool          vng_loop;

/* Seconds the previous frame took, set once by vangopix_core and read by anything that
   moves over time. One clock for the whole program: two modules keeping their own would
   drift apart on a stalled frame, and two panels sliding at visibly different speeds is
   the kind of thing that is only ever noticed as wrong. Clamped, so a window drag - which
   stops the loop dead for as long as the hand holds it - resumes rather than teleports. */
extern float         vng_dt;

/* Brings up SDL, the window, the renderer and the font, then turns every path in argv
   into a tab. False means nothing usable came up and the program should stop. */
extern bool vangopix_init (int argc, char **argv);

/* Tears down in the reverse order. Safe to call after a partial init. */
extern void vangopix_quit (void);

/* Builds a path to a file shipped beside the executable, whatever the platform and
   whatever directory the program was launched from. Free the result with SDL_free. */
extern char *vangopix_asset (const char *relative);

#endif
