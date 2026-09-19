#ifndef VANGOPIX_CLIPBOARD_H
#define VANGOPIX_CLIPBOARD_H

#include <SDL3/SDL.h>

/*
 * THE CLIPBOARD IS THE MACHINE'S, NOT THIS PROGRAM'S.
 *
 * It used to be one buffer inside select.c, which crossed tabs and crossed nothing else: a
 * screenshot, a sprite copied out of a browser or out of another editor could not come in,
 * and a copy made here could not go out. Now a copy is offered to the system as png, and a
 * paste takes THE LATEST IMAGE COPIED ANYWHERE - ours while ours is still what the system
 * holds, the other program's the moment it copied something after us.
 *
 * Nothing has to be told which: SDL calls the offer's cleanup when another program takes
 * the clipboard, so "ours is still there" is a flag that cleanup clears. Our own copy is
 * pasted from the buffer, never read back through the system - exact, and no decode.
 *
 * WHEN THE SYSTEM HOLDS NO IMAGE, OUR LAST COPY IS PASTED. A line of text copied in a browser
 * is newer, but it is not something this program can put on a sheet, and CTRL+V answering
 * with the last image beats answering with nothing.
 *
 * PNG, and only png, reaches another program on Windows: SDL's backend writes the FIRST
 * image type offered and stops (SDL_windowsclipboard.c, 3.4). png is the one that keeps
 * alpha, and pixel art is mostly alpha; the cost is a program that reads only CF_DIB, which
 * does not see it. Elsewhere the system asks for what it wants, so bmp is offered as well.
 */

/* Takes the buffer (the clipboard owns it from here) and offers it to the system. */
extern void clipboard_put (Uint32 *px, int w, int h);

/*
 * The latest image copied anywhere, as a fresh ARGB8888 buffer the CALLER owns, or NULL if
 * there is none. An image too big for a texture on this machine is refused IN A BOX, the way
 * an open is - a paste is a person asking for exactly that picture.
 */
extern Uint32 *clipboard_take (int *w, int *h);

/*
 * True keeps every copy inside the program and never reads the system's. For the check
 * suite, which runs on the real video driver and would otherwise overwrite the clipboard of
 * whoever ran `make test` - and would read whatever they had copied.
 */
extern void clipboard_local (bool on);

/*
 * An image file held in memory - what the system hands over - as an ARGB8888 buffer the
 * caller owns. NULL if SDL3_image cannot read it, or if a side is past `limit`: then *w and
 * *h still say how big it was, which is the difference between "not an image" and "too big".
 */
extern Uint32 *clipboard_decode (const void *data, size_t size, int limit, int *w, int *h);

extern void clipboard_free (void);

#endif
