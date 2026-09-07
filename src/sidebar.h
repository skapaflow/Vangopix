#ifndef VANGOPIX_SIDEBAR_H
#define VANGOPIX_SIDEBAR_H

#include "vangopix.h"

/*
 * The project sidebar: the only file that draws a folder.
 *
 * Separate from project.c the same way tabbar.c is separate from tabs.c - the model
 * knows about directories and file names, this knows about pixels and the mouse.
 *
 * It FLOATS over the sheet rather than pushing it aside, which is where it differs from
 * the code editor it is modelled on. A docked panel is exactly the permanent chrome this
 * program exists without, and the sidebar is summoned and dismissed like everything else
 * here. Making it push instead would be a change to one rectangle.
 *
 * It slides in and out rather than appearing: a panel that covers a third of the window
 * between two frames leaves nothing on screen to say where it came from.
 */

extern void sidebar_toggle  (void);

/* Where the panel is HEADED, not where it is - true from the moment it starts sliding
 * in. What asks is deciding whether to summon it, and one already on its way must not
 * be toggled straight back out. */
extern bool sidebar_visible (void);

/* Returns true when the sidebar consumed the event. */
extern bool sidebar_event (const SDL_Event *e);

/* How far the panel reaches into the window right now: its right edge while it is in or
   sliding, 0 while it is out. What anything asking "is the pointer over the panel?" needs
   without learning the panel's width - the same shape as tabbar_height. */
extern float sidebar_edge (void);

extern void sidebar_draw (void);

#endif
