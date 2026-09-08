#ifndef VANGOPIX_UNDO_H
#define VANGOPIX_UNDO_H

#include "vangopix.h"
#include "tabs.h"

/*
 * Undo, per document, and NOT MADE OF SNAPSHOTS.
 *
 * A snapshot of a 4000x4000 sheet is 64MB, and a person drawing makes a hundred strokes
 * in an afternoon. The first Vangopix already reached the answer: carry the PIXELS THAT
 * CHANGED - old colour and new colour, one entry each - so a stroke costs what the
 * stroke touched and nothing more. A pencil line across a big canvas is a few kilobytes.
 *
 * TWO KINDS OF STEP, AND THAT IS THE DECISION THIS FILE EXISTS TO MAKE.
 *
 * It would have been natural to write this as a list of pixel carries and stop. It would
 * also have been wrong on the first day: vng_tab_resize already exists, and dragging a
 * corner grip already changes the document. An undo that only knew about pixels would
 * take back a brush stroke and shrug at a resize, which is the kind of inconsistency a
 * person learns not to trust and then stops pressing CTRL+Z at all.
 *
 * So a step is either a run of pixel carries or a resize, and the stack is one list of
 * both, in the order they happened. Anything that changes a document from here on adds
 * a third kind rather than pretending to be one of these two.
 *
 * A RESIZE IS RECORDED WITHOUT COPYING ANYTHING. Resizing already allocates a new buffer
 * and throws the old one away - so the old one is handed here instead of being freed,
 * and it IS the undo record. Redo needs no buffer at all: replaying the resize from the
 * restored state is deterministic, since the uncovered area is white by definition.
 *
 * THE BUDGET IS THE ONE THING THAT CANNOT BE LEFT OUT. A bucket fill on a large canvas
 * is a single step holding one carry per pixel, and a resize step holds a whole buffer.
 * Without a ceiling, a long session grows until the machine gives out. The oldest steps
 * are dropped first, and the step being stood on is never dropped.
 */

typedef struct _vng_undo_ VNG_UNDO;

/* Frees the whole stack, including every buffer a resize step is holding. */
extern void undo_free (VNG_UNDO *u);

/*
 * A run of pixel changes. CALLED BY tabs.c AND NOTHING ELSE: a stroke opens and closes
 * through vng_tab_stroke_open / vng_tab_stroke_close, which is what makes it impossible
 * for a tool to change pixels and forget to record them.
 */
extern bool undo_open  (VNG_TAB *t);
extern void undo_carry (VNG_TAB *t, int x, int y, Uint32 was, Uint32 now);
extern void undo_close (VNG_TAB *t);   /* a step that carried nothing is discarded */

/* Puts back every pixel the OPEN step has changed so far and empties it, leaving the step
   open. What a write-through stroke does instead of throwing a preview away - see the
   `direct` strokes in tabs.h. */
extern void undo_rewind (VNG_TAB *t);

/*
 * A resize. TAKES OWNERSHIP of `was`, the buffer the resize replaced - it is freed with
 * the step, or handed back to the document when the step is undone.
 *
 * (w, h, dx, dy) are what the resize did, kept so redo can replay it.
 */
extern void undo_resize (VNG_TAB *t, Uint32 *was, int was_w, int was_h,
                         int w, int h, int dx, int dy);

extern bool undo_undo (VNG_TAB *t);
extern bool undo_redo (VNG_TAB *t);

/* Remembers the step the document was saved at, so undoing back to it clears the dirty
   mark instead of leaving a star on a file that matches its disk copy exactly. */
extern void undo_mark_saved (VNG_TAB *t);

#endif
