#ifndef VANGOPIX_TABS_H
#define VANGOPIX_TABS_H

#include "vangopix.h"

typedef struct _vng_undo_ VNG_UNDO;   /* undo.h owns it; opaque from here */

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

	/*
	 * THE OTHER TWO OF THE THREE BUFFERS A STROKE IS DRAWN OVER.
	 *
	 * pixels_preview holds the stroke in progress and nothing else; it is composited
	 * over the document until the hand comes up, and only then merged in. mask says,
	 * one byte per pixel, which pixels this stroke has already touched - and that is
	 * the whole reason it exists: without it a half transparent brush passing twice
	 * over the same pixel blends onto its own output and saturates transparency into
	 * opacity, which is the detail the first Vangopix documents at the top of its
	 * main.c.
	 *
	 * BOTH ARE ALLOCATED ON THE FIRST STROKE, NOT WITH THE DOCUMENT. They cost five
	 * bytes per pixel on top of the four the sheet already costs, and Vangopix is meant
	 * to be the machine's image viewer as much as its editor - a 6000x4000 photograph
	 * opened to be looked at would otherwise pay 216MB instead of 96MB for a brush
	 * nobody picked up.
	 */
	Uint32 *pixels_preview;
	Uint8  *mask;
	SDL_Texture *tex_preview;
	bool    stroke;            /* a stroke is open, so the preview has something in it */
	bool    direct;            /* ... unless it writes straight through - see stroke_open */
	int     sx0, sy0, sx1, sy1;/* what it has touched, half open, so nothing else is
	                            * uploaded or walked when it closes */

	VNG_UNDO *undo;            /* NULL until the document is first changed */

	/* The view is PER TAB, not global: switching tabs must put the drawing back
	 * where the eye left it, same zoom and same corner. A global view makes every
	 * switch cost a manual reframing. */
	float   zoom;
	float   off_x, off_y;

	struct _vng_tab_ *prev, *next;
} VNG_TAB;

extern VNG_TAB *vng_tabs;   /* first in the row  */
extern VNG_TAB *vng_tab;    /* the one on screen */

/*
 * A STROKE, WHICH IS ALSO AN UNDO STEP - the two open and close together on purpose,
 * so a tool cannot change the document and forget to record it.
 *
 *   open   allocates the two extra buffers if this is the first stroke, and starts a
 *          step. False means there was no memory for them and nothing may be drawn.
 *
 *          `direct` is the one flag, and it exists because A PREVIEW IS COMPOSITED OVER THE
 *          SHEET AND NOTHING COMPOSITED OVER ANYTHING CAN TAKE A PIXEL AWAY. An eraser laid
 *          into the preview is invisible until the merge - it looked as though rubbing out
 *          only happened when the button came up. The same is true of ANY colour with alpha
 *          below full, so the tool passes `direct` when what it lays is not opaque, and the
 *          writes go straight into the document with their carries as they go.
 *   touched  has this pixel already been painted in THIS stroke? What a blending tool
 *          asks before it blends, so it never blends onto its own output.
 *   put    writes one pixel into the preview and marks it. The colour is final; the
 *          tool has already decided what blending means for it.
 *   reset  throws the preview away WITHOUT closing the step, which is what a shape tool
 *          does on every motion: a line being dragged is redrawn from its anchor each
 *          time, not accumulated. The undo step stays open across it, so the whole drag
 *          is still one undo.
 *   close  merges every marked pixel into the document, records every one of them, and
 *          leaves the preview empty for the next stroke.
 */
extern bool vng_tab_stroke_open  (VNG_TAB *t, bool direct);
extern bool vng_tab_touched      (VNG_TAB *t, int x, int y);
extern void vng_tab_put          (VNG_TAB *t, int x, int y, Uint32 argb);
extern void vng_tab_stroke_reset (VNG_TAB *t);
extern void vng_tab_stroke_close (VNG_TAB *t);

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

/*
 * THE TWO CALLS UNDO NEEDS, AND NOBODY ELSE SHOULD WANT.
 *
 * resize_raw is vng_tab_resize without the undo record and without freeing the buffer it
 * replaces - it RETURNS that buffer, and the caller owns it. adopt is the other
 * direction: this buffer is the document now, and the one it displaces comes back.
 *
 * Both return NULL having changed nothing when the new texture cannot be made.
 */
extern Uint32 *vng_tab_resize_raw (VNG_TAB *t, int w, int h, int dx, int dy);
extern Uint32 *vng_tab_adopt      (VNG_TAB *t, Uint32 *pixels, int w, int h);

extern int      vng_tab_count (void);
extern int      vng_tab_index (VNG_TAB *t);
extern void     vng_tab_title (void);
extern void     vng_tabs_free (void);

#endif
