#ifndef VANGOPIX_KEYS_H
#define VANGOPIX_KEYS_H

#include "vangopix.h"

/*
 * WHO HAS THE KEYBOARD.
 *
 * The mouse never needed a module like this. The pointer has a POSITION, so the layer
 * chain in core.c settles every click by asking whatever is on top first - tabbar,
 * sidebar, grips, camera - and each one answers by looking at where the click landed.
 *
 * The keyboard has no position. Nothing about a TAB says whether it belongs to the
 * sidebar or to a name being typed into a field, and no amount of ordering derives it.
 * So the owner is STATED rather than deduced: at most one holder at a time, and while
 * there is one it gets every key event and the global shortcuts get none.
 *
 * This exists before the drawing tools rather than after them on purpose. A tool brings
 * twenty more bare keys, and every one of them is a key that must go quiet the moment a
 * field is open. Retrofitting that across twenty call sites is the expensive version of
 * this file.
 *
 * THE HALF THAT IS EASY TO MISS - and the reason this is not just a flag in core.c:
 * TWO MODULES READ THE KEYBOARD WITHOUT EVER SEEING AN EVENT. view.c polls
 * SDL_GetKeyboardState for SPACE, because space plus left drag pans; resize.c polls
 * SDL_GetModState for SHIFT, because a motion event carries no modifiers. Gating events
 * alone would leave both of them reading the hardware behind the owner's back: a space
 * typed into a filename would pan the sheet under a left drag, and a capital letter
 * would snap a grip to the grid. They go through keys_held and keys_mods, which report
 * NOTHING HELD while the keyboard is owned. A tool that wants SHIFT to constrain a line
 * or SPACE to pan mid-stroke must use the same two calls, and that is the whole contract
 * it has to know.
 */

/* Called with every keyboard event while this handler owns the keyboard. It does not
   report consumption: ownership already decided that. A handler that is finished calls
   keys_release from inside it - the event that ended it is still consumed. */
typedef void (*KEYS_HANDLER) (const SDL_Event *e, void *ctx);

/*
 * Takes the keyboard. ctx is handed back to fn and identifies the holder.
 *
 * Text input is started with it: SDL3 does not deliver SDL_EVENT_TEXT_INPUT until
 * SDL_StartTextInput is called, and typing is what every holder so far is for. A holder
 * that only wants ESC and ENTER simply ignores those events.
 *
 * A second capture REPLACES the first. Nothing here stacks: a modal that opens over a
 * modal is a decision to raise, not a default to provide.
 */
extern void keys_capture (KEYS_HANDLER fn, void *ctx);

/* Gives it back. Ignored unless ctx is the current holder, so a module that closed late
   cannot drop somebody else's keyboard. */
extern void keys_release (void *ctx);

extern bool keys_owned (void);

/* Offered the raw event, FIRST, before any layer. True means it was a keyboard event and
   the owner took it. Everything that is not a keyboard event falls straight through. */
extern bool keys_event (const SDL_Event *e);

/* Is that key held right now - false while the keyboard is owned. */
extern bool keys_held (SDL_Scancode sc);

/* The live modifiers - none while the keyboard is owned. */
extern SDL_Keymod keys_mods (void);

#endif
