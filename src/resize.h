#ifndef VANGOPIX_RESIZE_H
#define VANGOPIX_RESIZE_H

#include "vangopix.h"
#include "tabs.h"

/*
 * Resizing the canvas by its corners.
 *
 * Four small blue squares sit on the corners of the sheet. Dragging one moves that
 * corner; the opposite corner stays where it is, so the drag reads as pulling the paper
 * rather than as moving it.
 *
 * NOTHING IS REALLOCATED WHILE THE HAND MOVES. The drag only tracks an outline, and the
 * buffer is rebuilt once, on release. The old Vangopix did the same, and the reason is
 * not only cost: a live rebuild would destroy pixels on the way out of the canvas that
 * a hand coming back would expect to still be there.
 */

extern bool resize_event (const SDL_Event *e, VNG_TAB *t);

/*
 * IS THE POINTER ON A GRIP - or carrying one, which is the same answer for as long as the
 * button is down and the hand has wandered off it.
 *
 * What the TOOL asks so it can stand down: the crosshair, the tip outline and the glyph all
 * promise a stroke, and over a grip the next press is a resize. The press itself was never in
 * doubt - this file sits above tool.c in the chain and takes it - but a pointer that offers
 * what it will not do is worse on a five pixel target than on a big one, because the aim is
 * exactly what the person is unsure about.
 *
 * Answered from the live mouse position rather than from an event, for the same reason the
 * shape is decided every frame: nothing moves the pointer off a grip except motion, and by
 * then the frame that had to look right has already been drawn.
 */
extern bool resize_hot (VNG_TAB *t);

/* Draws the four handles, and the outline plus the size readout while dragging. */
extern void resize_draw (VNG_TAB *t);

#endif
