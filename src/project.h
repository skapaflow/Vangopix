#ifndef VANGOPIX_PROJECT_H
#define VANGOPIX_PROJECT_H

#include "vangopix.h"

#define VNG_PROJ_FILE "projects.vngproj"

/*
 * The projects: folders the person has handed to Vangopix, and the images inside them.
 *
 * This is the MODEL. It knows about directories and file names and nothing about pixels
 * on screen - sidebar.c draws it. Same split as tabs.c and tabbar.c.
 *
 * Directories are scanned LAZILY, when they are first expanded. A project folder can
 * hold thousands of files across dozens of subfolders, and walking all of it to add one
 * root would stall the program at the exact moment a person is dropping a folder in.
 * A code editor does the same, for the same reason.
 */
typedef struct _vng_node_ {
	char *path;                 /* full path on disk                             */
	char  name[128];            /* the last component, which is what is shown    */
	bool  is_dir;
	bool  open;                 /* directory only: expanded in the sidebar       */
	bool  scanned;              /* directory only: children have been read       */

	struct _vng_node_ *child;   /* first child                                   */
	struct _vng_node_ *next;    /* next sibling                                  */
} VNG_NODE;

/* The project roots, as a sibling list. */
extern VNG_NODE *vng_projects;

/* Adds a directory as a root. Refuses duplicates and anything that is not a directory.
   Does not scan it - the first expansion does that. */
extern bool project_add (const char *dir);

/* Removes a root and everything under it. Does nothing for a non-root node. */
extern void project_remove (VNG_NODE *root);

/*
 * Expands or collapses a directory, RE-READING it every time it is opened.
 *
 * It used to read once and remember for ever, which is what "scanned lazily" bought and what
 * it cost: a sprite exported from another program while Vangopix was running simply was not
 * in the list, and nothing on screen suggested the list was a photograph rather than a
 * window. Opening a folder is the moment a person is asking what is in it, so it is the
 * moment to go and look.
 *
 * The subfolders that are still there stay open, and are re-read too - see project_refresh.
 */
extern void project_toggle (VNG_NODE *dir);

/*
 * Re-reads every open folder under every root. What the sidebar calls as it comes up.
 *
 * The other moment a person asks what is on disk is the moment they summon the panel - and
 * unlike opening one folder, this one cannot know which folder they came for, so it walks
 * whatever is expanded. Only the expanded parts: a collapsed project is one row on screen
 * however many thousand files are under it, and walking those to draw nothing is the cost
 * the lazy scan was avoiding in the first place.
 *
 * WHAT IT KEEPS: a subfolder still on disk keeps being open. A subfolder that is gone takes
 * its expansion with it, which is the only answer that can be given about a folder that is
 * not there. Files carry no state, so they are simply the new list.
 */
extern void project_refresh (void);

/*
 * Pulls a ROOT out of the list and puts it back at `index` (0-based, clamped).
 *
 * THE LIST IS THE ORDER. There is no separate array of positions to keep in step with it,
 * which is the whole reason reordering is a relink and not a sort - the same decision
 * vng_tab_move made for the tab row, and this is that call turned on its side.
 *
 * ROOTS ONLY, and that is not a limitation to lift later. A root is somewhere a PERSON put
 * this program, so its place in the list is the person's to decide; a subfolder's place is a
 * fact about the directory it lives in, and letting a hand reorder it would be inventing an
 * order that the disk will contradict on the next scan.
 *
 * AND IT DOES NOT SAVE, which every other call that changes this list does.
 *
 * Reordering is a DRAG: the list is relinked on every motion event so the panel shows the
 * answer under the hand, and a hand held over a boundary shakes across it many times a
 * second. Writing projects.vngproj on each of those is a file rewritten sixty times for one
 * gesture. The caller saves once, when the hand lets go - which is also the only moment the
 * new order is a decision rather than a position the drag is passing through.
 */
extern void project_move (VNG_NODE *root, int index);

/* projects.vngproj, beside the executable: one absolute path per line. Loading skips
   paths that no longer exist, so a removed drive quietly drops out of the list rather
   than leaving a dead row that cannot be opened. */
extern bool project_load (void);
extern bool project_save (void);

extern void project_free (void);

/* True when the name ends in something SDL3_image can read. */
extern bool project_is_image (const char *name);

#endif
