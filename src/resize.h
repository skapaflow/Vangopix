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

/* Draws the four handles, and the outline plus the size readout while dragging. */
extern void resize_draw (VNG_TAB *t);

#endif
