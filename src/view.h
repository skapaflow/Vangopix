#ifndef VANGOPIX_VIEW_H
#define VANGOPIX_VIEW_H

#include "vangopix.h"
#include "tabs.h"

/*
 * The camera. Ported from the camera module of the author's Skyonara engine
 * (SKNE_CORE/src/camera/camera.c), which in turn carries the pan and zoom of the 2016
 * Skyonara.
 *
 * TWO IDEAS COME FROM THERE AND THEY ARE THE WHOLE MODULE:
 *
 * 1. ZOOM IN STEPS, NOT CONTINUOUS. With nearest filtering and pixel art, a fractional
 *    scale spreads the pixels unevenly - a one pixel line lands as one or two pixels
 *    depending where it falls, and the image boils as the camera moves. On integer
 *    steps every art pixel is an exact square and the image is still. The steps below
 *    1:1 are for LOOKING at an image, not for drawing on it; there the art shrinks and
 *    aliasing is unavoidable anyway.
 *
 * 2. ZOOM HAPPENS AT THE CURSOR. Capture the world point under the pointer before the
 *    scale changes, capture it again after, and add the difference to the offset. The
 *    point stays under the same pixel, and the picture grows out of where the eye is
 *    already looking instead of out of the corner of the window.
 *
 * WHAT IS DIFFERENT HERE:
 *
 *   - The camera is PER TAB, not global. Skyonara has one world and one fscale; an
 *     editor has a camera per document, or switching tabs would reframe every drawing.
 *     Every call therefore takes the tab it acts on.
 *   - Panning is on the middle button OR space plus left, which is what image editors
 *     do. Skyonara only had the middle button, having no drawing tool to get in the way.
 *   - The wheel notch damper is dropped - see view.c.
 */

/* Frames the sheet in the window: the largest integer zoom that fits, centred. Called
   automatically the first time a tab is drawn. */
extern void view_reset (VNG_TAB *t);

/*
 * PUTS THE SHEET BACK: one pixel to one, and the middle of the DOCUMENT in the middle of the
 * window. What the 0 key means, and the only framing gesture that needs no aim.
 *
 * It used to keep whatever was in the middle of the window in the middle of the window, on
 * the grounds that the centre of the document is rarely where the work is. That is true of a
 * 1:1 TOGGLE, which is what it was then - a thing you reach for mid-stroke to check a detail
 * at its real size, and which must not throw the eye across the drawing to do it.
 *
 * This is a different gesture wearing the same call. "Put it back" is what a hand reaches for
 * when it has zoomed off somewhere and lost the sheet, and answering that by keeping the
 * point it is lost at would be answering the wrong question. A reset goes home.
 */
extern void view_home (VNG_TAB *t);

/* Returns true when the view consumed the event. */
extern bool view_event (const SDL_Event *e, VNG_TAB *t);

/* Where the sheet lands on screen, in window pixels. */
extern SDL_FRect view_sheet_rect (VNG_TAB *t);

/* Document space to window space and back. screen_to_world is what a drawing tool will
   use to turn a cursor position into the pixel under it. */
extern SDL_FPoint view_world_to_screen (VNG_TAB *t, float wx, float wy);
extern SDL_FPoint view_screen_to_world (VNG_TAB *t, float sx, float sy);

#endif
