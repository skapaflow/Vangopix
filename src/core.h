#ifndef VANGOPIX_CORE_H
#define VANGOPIX_CORE_H

#include "vangopix.h"

/* The frame. Everything that happens between two presents happens in here. */

extern void vangopix_input (void);   /* drain the event queue */
extern void vangopix_core  (void);   /* draw and present      */
extern void vangopix_core_free (void); /* the frame's own textures */

/* Tiles the desk into a rectangle. What anything that has to show NOTHING over the sheet
   needs - the selection's hole, so far. Exposed rather than copied because two checkerboards
   in one program would drift apart the first time one of the greys was tuned. */
extern void vangopix_desk_rect (SDL_FRect r);

/*
 * The desk's own tile, for the shapes SDL cannot clip to - a circle, so far. Tiling handles
 * every rectangle; anything else has to lay the pattern down itself, and these are what it
 * must use so the two agree.
 *
 * 0xAARRGGBB, matching the ARGB8888 the tile texture is built in.
 */
#define VNG_CHECK    6
#define VNG_CHECK_A  0xFF252525u
#define VNG_CHECK_B  0xFF303030u

#endif
