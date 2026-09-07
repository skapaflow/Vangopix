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

/* Expands or collapses a directory, reading its children the first time. */
extern void project_toggle (VNG_NODE *dir);

/* projects.vngproj, beside the executable: one absolute path per line. Loading skips
   paths that no longer exist, so a removed drive quietly drops out of the list rather
   than leaving a dead row that cannot be opened. */
extern bool project_load (void);
extern bool project_save (void);

extern void project_free (void);

/* True when the name ends in something SDL3_image can read. */
extern bool project_is_image (const char *name);

#endif
