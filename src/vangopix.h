#ifndef VANGOPIX_H
#define VANGOPIX_H

/*
 * What the whole program can see. No logic lives here - only the handful of objects
 * every module needs to name in order to exist.
 */

#include <stdbool.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

extern SDL_Window   *vng_win;
extern SDL_Renderer *vng_ren;
extern int           vng_win_w, vng_win_h;
extern bool          vng_loop;

#endif
