#ifndef VANGOPIX_COLOUR_H
#define VANGOPIX_COLOUR_H

#include "vangopix.h"

/*
 * THE COLOUR WINDOW: WHERE A COLOUR THAT IS NOT ON THE SHEET YET COMES FROM.
 *
 * Everything else about colour here reads it off the drawing - CTRL absorbs, M averages what
 * is in hand, SHIFT+R rolls. All of those need a colour to already exist. This is the one
 * place a new one is made, which is exactly the gap it fills and the whole of its job.
 *
 * IT IS NOT A PALETTE PANEL PARKED DOWN THE SIDE. The author's own account of the work is
 * that a pixel artist settles on eight to sixteen colours and then picks from the drawing
 * constantly - so the tiles are not where the work happens, and a window summoned to
 * ESTABLISH those colours and then put away is the shape that matches. C raises it.
 *
 * A SATURATION-VALUE SQUARE WITH A HUE BAR, NOT A WHEEL. The first Vangopix drew a wheel, and
 * a wheel is prettier; a square is easier to hit the same colour on twice, because both axes
 * are straight and the eye can measure along them. In a program about exact pixels that wins.
 *
 * THE ALPHA BAR IS A REAL CONTROL AND NOT AN AFTERTHOUGHT, because in this program nothing IS
 * a colour: the second slot starts transparent, rubbing out is drawing with nothing, and a
 * half transparent shade is a thing people want on purpose.
 *
 * SIXTEEN SWATCHES, and they are the "eight to sixteen" said back. Click loads one into the
 * slot being edited; SHIFT+click stores the current colour into it.
 *
 * The window EDITS ONE SLOT AT A TIME and shows which - and it follows the slot rather than
 * owning it, so a colour absorbed with CTRL out on the sheet appears here without anything
 * having to be told.
 */

extern void colour_toggle  (void);
extern bool colour_visible (void);
extern void colour_free    (void);

#endif
