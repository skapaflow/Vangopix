#ifndef VANGOPIX_TABS_H
#define VANGOPIX_TABS_H

#include "vangopix.h"

/*
 * A TAB IS A DOCUMENT. There is no second structure.
 *
 * The previous Vangopix kept two parallel lists - VANGO_TABS (name, path, hash) and
 * VANGO_IMAGE (pixels, undo, view) - joined every frame by a hash of the name. Two
 * lists to keep in sync on every insert and every removal, and a join that can be
 * wrong: two documents sharing a name share a hash, so a tab would show the other
 * one's image.
 *
 * Here the pointer is the identity. No hash, no lookup, nothing to fall out of sync.
 */
typedef struct _vng_tab_ {
	/* identity */
	Uint32  id;                /* monotonic; see vng_tab_by_id                   */
	char   *path;              /* path on disk; NULL until the document is saved */
	char    name[64];          /* what the window title shows                    */
	bool    dirty;             /* modified since the last save                   */

	/* the sheet */
	int     w, h;
	Uint32 *pixels;            /* ARGB8888 - THE DOCUMENT                        */
	SDL_Texture *tex;          /* the copy the GPU sees                          */
	bool    tex_dirty;         /* the sheet changed and the texture doesn't know */

	/* The view is PER TAB, not global: switching tabs must put the drawing back
	 * where the eye left it, same zoom and same corner. A global view makes every
	 * switch cost a manual reframing. */
	float   zoom;
	float   off_x, off_y;

	struct _vng_tab_ *prev, *next;
} VNG_TAB;

extern VNG_TAB *vng_tabs;   /* first in the row  */
extern VNG_TAB *vng_tab;    /* the one on screen */

extern VNG_TAB *vng_tab_new   (int w, int h);
extern VNG_TAB *vng_tab_open  (const char *path);
extern void     vng_tab_close (VNG_TAB *t);
extern void     vng_tab_step  (int dir);
extern void     vng_tab_move  (VNG_TAB *t, int index);

/* Resizes the canvas. (dx, dy) is where the OLD origin lands inside the new buffer, so
   growing to the left is dx > 0 and growing to the right is dx == 0 - one call serves
   all four corners. Uncovered area comes out white. Returns false and changes nothing
   if the allocation fails. */
extern bool     vng_tab_resize (VNG_TAB *t, int w, int h, int dx, int dy);
/*
 * The tab with that id, or NULL if it has been closed.
 *
 * THE POINTER IS STILL THE IDENTITY - everywhere inside one frame, on one thread. The
 * id exists for the one thing a pointer cannot do: cross a thread and a frame boundary
 * and still be checkable. SDL's file dialog answers on whatever thread the OS gives it,
 * possibly frames later, and by then the tab that asked may have been closed and its
 * memory handed to a new one at the same address. Comparing a pointer against the list
 * would not catch that; an id that is never reused does.
 */
extern VNG_TAB *vng_tab_by_id (Uint32 id);

/* Points the tab at a file: takes a copy of the path and renames the tab from it. What
   save-as calls once the person has chosen where the document lives. */
extern void     vng_tab_set_path (VNG_TAB *t, const char *path);

extern int      vng_tab_count (void);
extern int      vng_tab_index (VNG_TAB *t);
extern void     vng_tab_title (void);
extern void     vng_tabs_free (void);

#endif
