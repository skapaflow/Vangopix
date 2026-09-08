#include "undo.h"

/* Enough for a long afternoon of strokes, small enough that a runaway fill cannot take
 * the machine with it. A pencil stroke is kilobytes; the ceiling exists for the bucket
 * on a large canvas and for the resize step, which holds a whole buffer. */
#define BUDGET  (128u * 1024u * 1024u)

typedef enum { STEP_PIXELS, STEP_RESIZE } STEP_KIND;

typedef struct { int x, y; Uint32 was, now; } CARRY;

typedef struct _step_ {
	STEP_KIND kind;

	/* STEP_PIXELS */
	CARRY *carry;
	int    lot, cap;

	/* STEP_RESIZE. `was` is owned here while the step is done, and handed back to the
	 * document while it is undone - which is exactly when it is NULL. */
	Uint32 *was;
	int     was_w, was_h;
	int     w, h, dx, dy;

	size_t  bytes;   /* what this step costs, for the budget */

	struct _step_ *prev, *next;
} STEP;

struct _vng_undo_ {
	STEP  *head;        /* oldest */
	STEP  *tail;        /* newest, whether applied or not */
	STEP  *top;         /* the last step APPLIED; NULL when everything is undone */
	STEP  *open;        /* the pixel step being filled right now */
	STEP  *saved;       /* where the document last matched its file */
	bool   saved_lost;  /* that step fell off the bottom, so dirty can never clear */
	size_t bytes;
};

/* ------------------------------------------------------------------ housekeeping */

static void step_free (STEP *s)
{
	if (!s) return;
	SDL_free(s->carry);
	SDL_free(s->was);     /* NULL while the step is undone: the document holds it */
	SDL_free(s);
}

void undo_free (VNG_UNDO *u)
{
	if (!u) return;

	for (STEP *s = u->head; s; ) {
		STEP *n = s->next;
		step_free(s);
		s = n;
	}
	step_free(u->open);
	SDL_free(u);
}

static VNG_UNDO *stack_of (VNG_TAB *t)
{
	if (!t) return NULL;
	if (!t->undo) t->undo = (VNG_UNDO *) SDL_calloc(1, sizeof *t->undo);
	return t->undo;
}

/* The star in the title is a question about the stack, not a flag somebody remembers to
 * set: the document is dirty when it is not standing where it was saved. Undoing back to
 * that point clears it, which a bool never could. */
static void mark (VNG_TAB *t, VNG_UNDO *u)
{
	t->dirty = u->saved_lost || u->top != u->saved;
	vng_tab_title();
}

/* Everything after the top is a future that just stopped existing. */
static void drop_redo (VNG_UNDO *u)
{
	STEP *s = u->top ? u->top->next : u->head;

	while (s) {
		STEP *n = s->next;
		u->bytes -= s->bytes;
		if (s == u->saved) u->saved_lost = true;
		step_free(s);
		s = n;
	}

	u->tail = u->top;
	if (u->top) u->top->next = NULL; else u->head = NULL;
}

/* Oldest first, and never the step being stood on - the one thing a person is about to
 * press CTRL+Z on. A single step larger than the whole budget is kept: one huge undo is
 * still better than none. */
static void trim (VNG_UNDO *u)
{
	while (u->bytes > BUDGET && u->head && u->head != u->top) {
		STEP *s = u->head;

		u->head = s->next;
		if (u->head) u->head->prev = NULL; else u->tail = NULL;

		u->bytes -= s->bytes;
		if (s == u->saved) u->saved_lost = true;
		step_free(s);
	}
}

static void push (VNG_TAB *t, VNG_UNDO *u, STEP *s)
{
	drop_redo(u);

	s->prev = u->tail;
	s->next = NULL;
	if (u->tail) u->tail->next = s; else u->head = s;
	u->tail = s;
	u->top  = s;

	u->bytes += s->bytes;
	trim(u);
	mark(t, u);
}

/* ------------------------------------------------------------------ pixel steps */

bool undo_open (VNG_TAB *t)
{
	VNG_UNDO *u = stack_of(t);
	if (!u) return false;

	/* A stroke that was never closed - a tool dropped mid-way by a lost mouse button -
	 * is thrown away rather than merged into the next one. */
	step_free(u->open);

	u->open = (STEP *) SDL_calloc(1, sizeof *u->open);
	if (!u->open) return false;

	u->open->kind = STEP_PIXELS;
	return true;
}

void undo_carry (VNG_TAB *t, int x, int y, Uint32 was, Uint32 now)
{
	VNG_UNDO *u = t ? t->undo : NULL;
	if (!u || !u->open) return;

	STEP *s = u->open;

	if (s->lot == s->cap) {
		/* Doubling, starting at a size that covers an ordinary short stroke without
		 * any growth at all. */
		int    cap = s->cap ? s->cap * 2 : 256;
		CARRY *px  = (CARRY *) SDL_realloc(s->carry, (size_t)cap * sizeof *px);
		if (!px) return;   /* the drawing still happens; only its record is short */

		s->carry = px;
		s->cap   = cap;
	}

	s->carry[s->lot].x   = x;
	s->carry[s->lot].y   = y;
	s->carry[s->lot].was = was;
	s->carry[s->lot].now = now;
	s->lot++;
}

void undo_rewind (VNG_TAB *t)
{
	VNG_UNDO *u = t ? t->undo : NULL;
	if (!u || !u->open) return;

	STEP *s = u->open;

	/* Backwards, so a pixel written twice in this step ends on the colour it had before the
	 * first of them. The carries are the only record of what was there. */
	for (int i = s->lot - 1; i >= 0; i--) {
		const CARRY *c = &s->carry[i];
		if (c->x < 0 || c->y < 0 || c->x >= t->w || c->y >= t->h) continue;
		t->pixels[(size_t)c->y * t->w + c->x] = c->was;
	}

	s->lot = 0;             /* the allocation is kept: the next pass will refill it */
	t->tex_dirty = true;
}

void undo_close (VNG_TAB *t)
{
	VNG_UNDO *u = t ? t->undo : NULL;
	if (!u || !u->open) return;

	STEP *s = u->open;
	u->open = NULL;

	/* A click that changed nothing is not a thing to undo. */
	if (s->lot == 0) { step_free(s); return; }

	s->bytes = (size_t)s->lot * sizeof(CARRY);
	push(t, u, s);
}

/* ------------------------------------------------------------------ resize steps */

void undo_resize (VNG_TAB *t, Uint32 *was, int was_w, int was_h,
                  int w, int h, int dx, int dy)
{
	VNG_UNDO *u = stack_of(t);
	if (!u) { SDL_free(was); return; }

	STEP *s = (STEP *) SDL_calloc(1, sizeof *s);
	if (!s) { SDL_free(was); return; }

	s->kind  = STEP_RESIZE;
	s->was   = was;                /* ownership moves here */
	s->was_w = was_w;
	s->was_h = was_h;
	s->w = w; s->h = h; s->dx = dx; s->dy = dy;
	s->bytes = (size_t)was_w * was_h * sizeof(Uint32);

	push(t, u, s);
}

/* ------------------------------------------------------------------ walking it */

static void apply_pixels (VNG_TAB *t, STEP *s, bool forward)
{
	for (int i = 0; i < s->lot; i++) {
		const CARRY *c = &s->carry[forward ? i : s->lot - 1 - i];

		/* A carry from a different geometry cannot land, and must not be allowed to
		 * write outside the buffer. In a stack walked in order this never fires; it is
		 * here because the cost is one comparison and the alternative is a stray write
		 * into somebody else's memory. */
		if (c->x < 0 || c->y < 0 || c->x >= t->w || c->y >= t->h) continue;

		t->pixels[(size_t)c->y * t->w + c->x] = forward ? c->now : c->was;
	}
	t->tex_dirty = true;
}

bool undo_undo (VNG_TAB *t)
{
	VNG_UNDO *u = t ? t->undo : NULL;
	if (!u || !u->top) return false;

	STEP *s = u->top;

	if (s->kind == STEP_PIXELS) {
		apply_pixels(t, s, false);
	} else {
		/* The buffer this resize replaced goes back to being the document, and the one
		 * the resize produced is thrown away - redo can build it again from scratch,
		 * which is cheaper than carrying it around for a redo that may never come. */
		Uint32 *now = vng_tab_adopt(t, s->was, s->was_w, s->was_h);
		if (!now) return false;      /* nothing changed: the texture could not be made */

		SDL_free(now);
		s->was = NULL;               /* the document owns it again */

		/* The resize shifted the camera so the drawing would not jump. Undoing it has
		 * to shift back, for exactly the same reason. */
		t->off_x -= s->dx;
		t->off_y -= s->dy;
	}

	u->top = s->prev;
	mark(t, u);
	return true;
}

bool undo_redo (VNG_TAB *t)
{
	VNG_UNDO *u = t ? t->undo : NULL;
	if (!u) return false;

	STEP *s = u->top ? u->top->next : u->head;
	if (!s) return false;

	if (s->kind == STEP_PIXELS) {
		apply_pixels(t, s, true);
	} else {
		/* Replays the resize, and takes back the buffer it replaces - which is the same
		 * buffer this step was holding before it was undone. */
		Uint32 *was = vng_tab_resize_raw(t, s->w, s->h, s->dx, s->dy);
		if (!was) return false;

		/* was_w/was_h still describe it: they are the geometry from before this resize,
		 * which is exactly what the document was standing at a moment ago. */
		s->was = was;

		t->off_x += s->dx;
		t->off_y += s->dy;
	}

	u->top = s;
	mark(t, u);
	return true;
}

void undo_mark_saved (VNG_TAB *t)
{
	VNG_UNDO *u = stack_of(t);
	if (!u) return;

	u->saved      = u->top;
	u->saved_lost = false;
	mark(t, u);
}
