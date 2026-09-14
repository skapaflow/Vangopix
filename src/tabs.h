#ifndef VANGOPIX_TABS_H
#define VANGOPIX_TABS_H

#include "vangopix.h"

typedef struct _vng_undo_ VNG_UNDO;   /* undo.h owns it; opaque from here */
typedef struct _vng_sel_  VNG_SEL;    /* select.h, likewise */
typedef struct _vng_pal_  VNG_PAL;   /* palette.h, likewise */
typedef struct _vng_anim_ VNG_ANIM;  /* anim.h, likewise */

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

	/*
	 * WHAT THE TEXTURE HAS NOT SEEN YET, as a rectangle and not as a flag - half open, empty
	 * when tx1 <= tx0. See vng_tab_touch.
	 *
	 * It was a bool, and a bool can only say "all of it": every write-through stroke - the
	 * eraser, any colour with alpha - set it on every frame the hand moved, and the frame
	 * then sent the whole sheet to the GPU. On a 4000 pixel photograph that is sixty-four
	 * megabytes a frame to rub out a dozen pixels. The preview texture keeps its own for the
	 * same reason: it is re-sent only where it changed, not wherever the stroke has been.
	 */
	int     tx0, ty0, tx1, ty1;
	int     px0, py0, px1, py1;

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
	SDL_Rect clip;             /* where it may write: the selection, or the whole sheet -
	                            * see vng_tab_stroke_open. Always inside the sheet. */

	VNG_UNDO *undo;            /* NULL until the document is first changed */

	/* The selection is PER TAB, which is half of what makes copying between documents
	 * simple: the clipboard is one buffer for the program and a float belongs to the sheet
	 * it is over, so it can neither leak into another tab nor outlive this one. */
	VNG_SEL  *sel;

	/* The palette is PER TAB for the same reason, and it is the first Vangopix's own
	 * arrangement: a palette is the working set of ONE drawing, not of the program. Built
	 * lazily on first sight - a photograph opened only to be looked at pays nothing for a
	 * panel nobody summoned - and freed with the tab it belongs to. */
	VNG_PAL  *pal;

	/* The animation clips are PER TAB too, and the reason is a file that got written wrong: a
	 * clip is four numbers pointing at pixels IN THIS DRAWING and it is saved beside this image
	 * (hero.png keeps hero.vnganime). One list for the program followed the tab only when the
	 * window was summoned, so a player left open while the sheet changed underneath it saved
	 * one image's clips into the other's sidecar. Built on first sight, freed with the tab. */
	VNG_ANIM *anim;

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
 *
 * WHILE A RECTANGLE IS MARKED, NO COLOUR LANDS OUTSIDE IT - and this is where that is true,
 * not in the tools. A stroke takes its `clip` from the selection (select_area) when it OPENS,
 * the same moment a tool takes its colour, and `put` refuses every pixel outside it. With
 * nothing marked the clip is the whole sheet. So no tool asks about the selection and no tool
 * can forget to: the pencil, the bucket and whatever is written next are held to it by the
 * only road into the document, exactly as they are held to undo.
 *
 * WHICH IS WHY A NEW TOOL MUST WRITE THROUGH open + put AND NEVER INTO t->pixels. A direct
 * write skips the selection and the undo step both - it is wrong twice before it is wrong
 * in any way of its own.
 *
 *   writable  may the open stroke write here: inside the sheet AND inside the clip. False
 *          with no stroke open. What a tool that WALKS a region asks, because a walk has to
 *          take the edge as a wall rather than merely be refused at it - see plot_flood.
 *
 *   open_unclipped  THE ONE EXCEPTION, AND IT HAS ONE CALLER: select.c's put_down, which is
 *          what landing a float and throwing a cut one away both are. Putting a float down
 *          writes the hole it left AND the place it landed, and by then the marked rectangle
 *          is the second of those - bounding the selection's own move by the selection would
 *          leave the hole unwritten. The selection is what says where the edge is; it cannot
 *          be held inside itself. A second caller is a decision to raise.
 *
 * A PUT THAT CHANGES NOTHING RECORDS NOTHING. Black laid over black is marked - so a blending
 * tool still will not come back to it - but carries no undo entry, and a stroke made only of
 * such pixels leaves no step: CTRL+Z on it would seem to do nothing, which reads as undo being
 * broken. It is the rule undo_close already applied to a click that touched no pixel at all.
 *
 * ONE STROKE AT A TIME PER SHEET. Opening a second one while the first is still open used to
 * throw the first one's undo step away and forget its rectangle, leaving its preview pixels
 * behind to be merged by whichever stroke came next. It now CANCELS the first - rewound or
 * wiped, nothing of it left - which is a safety net and not a protocol: whoever is about to
 * change the sheet calls vng_tab_settle first, and then there is nothing open to cancel.
 */
extern bool vng_tab_stroke_open  (VNG_TAB *t, bool direct);
extern bool vng_tab_stroke_open_unclipped (VNG_TAB *t, bool direct);
extern bool vng_tab_writable     (VNG_TAB *t, int x, int y);
extern bool vng_tab_touched      (VNG_TAB *t, int x, int y);
extern void vng_tab_put          (VNG_TAB *t, int x, int y, Uint32 argb);
extern void vng_tab_stroke_reset (VNG_TAB *t);
extern void vng_tab_stroke_close (VNG_TAB *t);

/*
 * THE DOCUMENT CHANGED INSIDE THIS RECTANGLE, and the texture has not been told.
 *
 * Every write to t->pixels outside a stroke calls it - undo, the adopt of a new buffer - and
 * the strokes call it themselves. Clamped to the sheet here, so a caller may pass a rectangle
 * that runs off the edge. vng_tab_upload is the other half: it sends exactly the union of
 * everything touched since the last frame, and nothing that was not.
 */
extern void vng_tab_touch  (VNG_TAB *t, int x, int y, int w, int h);
extern void vng_tab_upload (VNG_TAB *t);

extern VNG_TAB *vng_tab_new   (int w, int h);

/*
 * Opens anything SDL3_image can read, as a new tab - OR SHOWS THE TAB THAT ALREADY HOLDS THAT
 * FILE. Two tabs editing one file is two futures for it, and whichever is saved second
 * silently throws away the first; a person dropping a file that is already open is asking to
 * see it, not to fork it. A file that will not open says why in a message box: every caller
 * is a person who asked for exactly that file, and with no console the log says it to nobody.
 */
extern VNG_TAB *vng_tab_open  (const char *path);

/* Closes it and frees everything it owns. The tab on screen stays on screen unless it IS the
   one being closed - the [x] on a tab behind the current one used to take the eye with it. */
extern void     vng_tab_close (VNG_TAB *t);

/*
 * ENDS EVERYTHING IN FLIGHT ON THIS SHEET, so what comes next sees the document as it will
 * stay: the tool's open stroke (a drag is kept - the person watched it appear - and the SHIFT
 * line preview is thrown away, having never been a decision), and a floating selection, which
 * is put down.
 *
 * WHY IT EXISTS: a stroke or a float is state that lives BETWEEN events, and every operation
 * that replaces or reads the whole sheet used to meet it half done. A tab switched with the
 * pencil's SHIFT preview open kept that preview in the old sheet's buffers, and the next
 * stroke drawn there merged it in; an undo pressed mid-way through a write-through stroke was
 * undone again by that stroke's own rewind a frame later; a save with a float in the air wrote
 * the sheet without it. One call, made by each of those - switching (vng_tab_show), saving,
 * closing, quitting, undo and redo, a resize - instead of each of them knowing what can be
 * left open.
 */
extern void     vng_tab_settle (VNG_TAB *t);

/*
 * THE ONE WAY TO CHANGE WHICH DOCUMENT IS ON SCREEN.
 *
 * It exists because a switch is not just an assignment: the OUTGOING sheet is settled first -
 * a floating selection put down, an open stroke finished - or what was in flight is lost, or
 * worse, left half done in a sheet nobody is looking at. That is one of the bugs the first
 * Vangopix had between tabs, and scattering the fix across every place that assigned the
 * pointer is how it would come back.
 *
 * vng_tab_close is the exception and assigns directly: by the time it picks a fallback, the
 * tab that was current has already been freed, and there is nothing left to commit into.
 */
extern void     vng_tab_show  (VNG_TAB *t);

extern void     vng_tab_step  (int dir);
extern void     vng_tab_move  (VNG_TAB *t, int index);

/* Resizes the canvas. (dx, dy) is where the OLD origin lands inside the new buffer, so
   growing to the left is dx > 0 and growing to the right is dx == 0 - one call serves
   all four corners. Uncovered area comes out white. Returns false and changes nothing
   if the allocation fails or a side is past vng_tab_side_limit. The marked selection
   moves with the pixels, so the edge every tool is held to stays on the same drawing. */
extern bool     vng_tab_resize (VNG_TAB *t, int w, int h, int dx, int dy);

/*
 * THE LONGEST SIDE A SHEET CAN HAVE ON THIS MACHINE: the renderer's largest texture, because a
 * document IS a texture and one past this cannot be drawn at all. Asked of the renderer once;
 * VNG_MAX_SIDE when it will not say.
 *
 * It is the wall, not the default. VNG_MAX_SIDE is what a new sheet may be asked for; an image
 * opened from disk may already be bigger than that, and a corner grip may keep it that big -
 * but nothing may ask for a buffer the GPU would then refuse, which is what a grip pulled far
 * out at 1/16 zoom used to do, gigabytes at a time.
 */
extern int      vng_tab_side_limit (void);
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
