#ifndef VANGOPIX_THUMB_H
#define VANGOPIX_THUMB_H

#include "vangopix.h"
#include "tabs.h"

/*
 * THE THUMBNAIL IS NOT A MINIATURE, AND THAT IS THE FIRST VANGOPIX'S IDEA KEPT.
 *
 * It shows the document at 1:1 - one art pixel to one screen pixel - cropped around whatever
 * the main camera is looking at. Pixel art is DRAWN at 800% and LOOKED AT at 100%, and the
 * question a person asks twenty times an hour is "what does this actually look like?".
 *
 * A shrunk-down overview - the thing the word thumbnail usually means - answers a different
 * question, "where am I", and in this program the sheet already answers that by filling the
 * window. Building the overview instead would have been the obvious choice and the wrong
 * one.
 *
 * THREE THINGS ARE FIXED RATHER THAN CARRIED OVER, all of them in gui_thumbnail.c:
 *
 *   1. THE OUTLINE LIED. It drew the region marker at the CENTRE of the screen
 *      (`rectcenter(screen.x, screen.y, ...)`), but the source rectangle is clamped to the
 *      image - so near any edge the panel showed one part of the sheet and the outline
 *      pointed at another. Here the outline is computed from the same rectangle the panel
 *      draws from, so the two cannot disagree.
 *
 *   2. NOTHING WAS DRAWN BEHIND IT, so a transparent document showed whatever happened to be
 *      under the window. The desk goes down first, and transparency reads as transparency.
 *
 *   3. `rec4i win = {250, 100, w, h}` carried two numbers that were never read. Gone.
 *
 * IT IS A READOUT AND TAKES NO GESTURE OF ITS OWN. Click-to-navigate was considered and left
 * out: the panel is a crop around where the camera already IS, so clicking inside it would
 * move the view a few pixels - a gesture that looks useful and is not. It follows the camera
 * rather than being panned separately, because there is one place you are looking and two of
 * them would have to be kept in step by hand.
 *
 * Summoned by V and gone again, bottom right - opposite the colour slots and clear of the
 * project sidebar.
 */

extern void thumb_toggle  (void);
extern bool thumb_visible (void);

/* Returns true when the panel consumed the event: it floats over the sheet, so a click on it
   must not also land on the drawing underneath. */
extern bool thumb_event (const SDL_Event *e, VNG_TAB *t);

/* Drawn AFTER the sheet, because it copies from the sheet's own texture and that texture is
   brought up to date there. */
extern void thumb_draw (VNG_TAB *t);

#endif
