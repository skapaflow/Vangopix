#ifndef VANGOPIX_SPLASH_H
#define VANGOPIX_SPLASH_H

#include "vangopix.h"

/*
 * THE SPLASH: the picture the program opens with, and the only place it says its own name.
 *
 * WHY IT IS ALLOWED TO EXIST HERE. main.c rules out permanent chrome by name - a strip of
 * screen spent whether it is wanted or not - and a splash is the opposite of that: it spends
 * the WHOLE screen and then spends none of it, for as long as the first keystroke takes. It
 * is not chrome; it is the one moment before the work starts.
 *
 * IT LEAVES ON THE FIRST PRESS, key or button, and it swallows that press. A splash that let
 * the dismissing click through would open the project panel on the way out, and the person
 * would have dismissed one thing and summoned another with one gesture.
 *
 * On the RELEASE and not the press, which is what makes it a click rather than half of one -
 * and what stops the release of the dismissing click landing on whatever the splash was
 * covering.
 *
 * ---------------------------------------------------------------------------------------
 *
 * THE PICTURE AND ITS WORDS ARE ONE TEXTURE, composed once when the program starts.
 *
 * icon/vangopix_splash_screen.png is 448 x 512 with a white band across the bottom 64 pixels,
 * left there to be written in. The words go INTO the picture rather than beside it, and that
 * is not tidiness: a window smaller than 448 x 512 has to shrink the whole thing, and text
 * drawn afterwards in screen pixels would keep its own size while the art around it shrank -
 * a caption sliding out of its own band. Composed first and scaled as one, they cannot come
 * apart.
 *
 * It is also drawn once instead of laid out every frame, which is the cheaper half of the
 * same decision.
 *
 * CENTRED, AND AT ITS OWN SIZE. A maximised window gets the same picture in the middle of it,
 * not a 448 pixel image stretched across a 4K screen - which is what "the splash looks wrong
 * on a big monitor" always turns out to be. It shrinks only when it has to.
 */

/* Loads and composes it. Not fatal: no file, no font or no memory costs the splash and
   nothing else, and the program comes up on the desk as if it had already been dismissed. */
extern void splash_open (void);

extern bool splash_up (void);

/* Offered every event, FIRST. True when the splash took it - which is every press while it
   is showing, and nothing at all once it is gone. */
extern bool splash_event (const SDL_Event *e);

/* Drawn LAST, over everything, for the same reason it is offered events first. */
extern void splash_draw (void);

extern void splash_free (void);

#endif
