#ifndef VANGOPIX_SIDEBAR_H
#define VANGOPIX_SIDEBAR_H

#include "vangopix.h"
#include "project.h"

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

/*
 * ACTIVE BUT PUT AWAY: THE STRIP AT THE LEFT EDGE.
 *
 * With no document there is nothing to draw on and nothing to draw with, and the one thing a
 * person arriving wants is a way in. The keys written across the desk are one; this is the
 * other, and it is the only part of this interface that can be used by pointing at it - a
 * narrow strip with a folder on it, which slides the panel out when pressed.
 *
 * IT IS THERE ONLY WHILE THERE IS NO SHEET, and that limit is the whole reason it is allowed
 * to exist. main.c rules out permanent chrome by name - "a strip of screen spent whether it
 * is wanted or not" - and a handle parked down the side of somebody's drawing for ever is
 * exactly that. On an empty desk nothing is being spent: there is no artwork for it to be in
 * front of, and the screen is already a page explaining the program.
 *
 * The moment a sheet exists the strip is gone and the panel is summoned with TAB, like
 * everything else here.
 */

/* How far the panel reaches into the window right now: its right edge while it is in or
   sliding, the strip's width while the strip is showing, 0 otherwise. What anything asking
   "is the pointer over the panel?" needs without learning the panel's width - the same shape
   as tabbar_height. */
extern float sidebar_edge (void);

extern void sidebar_draw (void);

/*
 * The node on row `i` of the panel as it would be drawn now, and its depth; NULL past the
 * last row. `depth` may be NULL.
 *
 * Exported for the reason win_top is: which rows the panel shows is decided by a walk of the
 * tree, and a check cannot read that walk off the screen - the suite runs with no face loaded,
 * so there are no names on it to count.
 */
extern VNG_NODE *sidebar_row (int i, int *depth);

/*
 * THE PREVIEW UNDER THE POINTER, and it is drawn separately for the same reason the palette
 * under ALT is: it exists only while a hand is holding still on a name, and nothing summoned
 * that way should come up BEHIND something parked. sidebar_draw runs with the panels, in the
 * middle of the chain; this runs at the very end, over the windows and the tab bar.
 *
 * WHY A FILE LIST NEEDS ONE AT ALL. A folder of sprites is thirty names that all read
 * `walk_02.png`, and the only way to tell them apart today is to open each one, which costs a
 * tab. A preview is what makes the list answer the question the list is being asked.
 *
 * IT READS THE FILE OFF DISK, which nothing else in this panel does - the model knows about
 * names and the panel knew about pixels only in the sense of drawing text. That is why it
 * WAITS: the pointer has to rest on a name before anything is read, so sweeping down a list
 * costs nothing, and a 24 megapixel photograph is only ever opened by somebody who meant to
 * look at it. One preview is held at a time and it is dropped the moment the pointer leaves.
 *
 * The read is on this thread, so a very large file stalls the frame it is read in. That is a
 * deliberate limit and not an oversight: the alternative is a loader thread, which is a much
 * bigger decision than a thumbnail should be allowed to make on its own.
 */
extern void sidebar_hover_draw (void);

/* Lets go of the preview it is holding, if any. */
extern void sidebar_free (void);

#endif
