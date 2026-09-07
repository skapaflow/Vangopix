#ifndef VANGOPIX_TABBAR_H
#define VANGOPIX_TABBAR_H

#include "vangopix.h"

/*
 * The tab bar: the only piece of the interface that draws tabs.
 *
 * It is SEPARATE from tabs.c on purpose. tabs.c owns the documents and knows nothing
 * about pixels on screen; this file owns pixels on screen and knows nothing about what
 * a document contains. The day VagrantUI draws this bar instead, this file is the only
 * one that changes.
 *
 * IT IS NOT PERMANENT CHROME. It is off until TAB is pressed, it floats over the sheet
 * rather than pushing it down, and it costs nothing while it is not asked for. Toggle
 * rather than hold-to-show because a tab can be dragged to reorder, and a bar that
 * vanishes when a key is released cannot survive the drag.
 */

extern void tabbar_toggle  (void);
extern bool tabbar_visible (void);

/* Returns true when the bar consumed the event, so the caller stops handling it. */
extern bool tabbar_event (const SDL_Event *e);

extern void tabbar_draw (void);

#endif
