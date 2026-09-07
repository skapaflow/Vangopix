/* Headless exercise of the undo stack: resize both ways, strokes, and the dirty mark. */
#include "vangopix.h"
#include "tabs.h"
#include "undo.h"

static int fails = 0;

static void ok (const char *what, bool cond)
{
	SDL_Log("%s %s", cond ? "PASS" : "FAIL", what);
	if (!cond) fails++;
}

static bool same (const Uint32 *a, const Uint32 *b, int n)
{
	for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
	return true;
}

int main (void)
{
	if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("init: %s", SDL_GetError()); return 2; }
	if (!SDL_CreateWindowAndRenderer("t", 320, 240, SDL_WINDOW_HIDDEN,
	                                 &vng_win, &vng_ren)) {
		SDL_Log("window: %s", SDL_GetError());
		return 2;
	}

	VNG_TAB *t = vng_tab_new(4, 3);
	if (!t) { SDL_Log("no tab"); return 2; }

	/* A pattern nothing could produce by accident. */
	for (int i = 0; i < 4 * 3; i++) t->pixels[i] = 0xFF000000u | (Uint32)(i * 0x010203);

	Uint32 origin[12];
	SDL_memcpy(origin, t->pixels, sizeof origin);

	undo_mark_saved(t);
	ok("clean after mark_saved", t->dirty == false);

	/* ---- shrink, which destroys pixels that only a copy can bring back ---- */
	ok("resize 4x3 -> 2x2", vng_tab_resize(t, 2, 2, 0, 0));
	ok("geometry is 2x2", t->w == 2 && t->h == 2);
	ok("dirty after resize", t->dirty == true);

	Uint32 shrunk[4];
	SDL_memcpy(shrunk, t->pixels, sizeof shrunk);
	ok("shrink kept the top-left corner",
	   shrunk[0] == origin[0] && shrunk[1] == origin[1] &&
	   shrunk[2] == origin[4] && shrunk[3] == origin[5]);

	ok("undo the resize", undo_undo(t));
	ok("geometry back to 4x3", t->w == 4 && t->h == 3);
	ok("PIXELS RESTORED EXACTLY", same(t->pixels, origin, 12));
	ok("clean again after undoing to the saved point", t->dirty == false);

	ok("redo the resize", undo_redo(t));
	ok("geometry 2x2 again", t->w == 2 && t->h == 2);
	ok("redo reproduced the shrink", same(t->pixels, shrunk, 4));

	/* The second undo is the one that proves redo took the old buffer back. */
	ok("undo a second time", undo_undo(t));
	ok("pixels restored a second time", t->w == 4 && t->h == 3 &&
	                                    same(t->pixels, origin, 12));

	/* ---- the camera shift travels with it ---- */
	t->off_x = 100.0f; t->off_y = 50.0f;
	vng_tab_resize(t, 6, 5, 2, 1);            /* grew leftward and upward */
	ok("camera not yet moved by the resize itself", t->off_x == 100.0f);
	t->off_x += 2; t->off_y += 1;             /* what resize.c does afterwards */
	undo_undo(t);
	ok("camera shifted back by undo", t->off_x == 100.0f && t->off_y == 50.0f);
	ok("geometry back to 4x3 after the grow was undone", t->w == 4 && t->h == 3);

	/* ---- a stroke ---- */
	ok("stroke opens", vng_tab_stroke_open(t));
	ok("nothing touched yet", vng_tab_touched(t, 1, 1) == false);
	vng_tab_put(t, 1, 1, 0xFF00FF00u);
	vng_tab_put(t, 2, 1, 0xFF00FF00u);
	ok("touched after put", vng_tab_touched(t, 1, 1));
	ok("document untouched while the stroke is open", t->pixels[1 * 4 + 1] == origin[5]);

	vng_tab_stroke_close(t);
	ok("stroke merged into the document",
	   t->pixels[1 * 4 + 1] == 0xFF00FF00u && t->pixels[1 * 4 + 2] == 0xFF00FF00u);
	ok("the rest of the sheet is untouched",
	   t->pixels[0] == origin[0] && t->pixels[11] == origin[11]);
	ok("mask cleared for the next stroke", vng_tab_touched(t, 1, 1) == false);

	ok("undo the stroke", undo_undo(t));
	ok("STROKE UNDONE PIXEL BY PIXEL", same(t->pixels, origin, 12));
	ok("redo the stroke", undo_redo(t));
	ok("stroke back", t->pixels[1 * 4 + 1] == 0xFF00FF00u);

	/* ---- an empty stroke is not a step ---- */
	vng_tab_stroke_open(t);
	vng_tab_stroke_close(t);
	ok("an empty stroke left nothing to undo, so this undoes the real one",
	   undo_undo(t) && same(t->pixels, origin, 12));

	/* Each new action after a full undo discarded the future it was standing in front
	 * of, so by now the stack really is empty and undo has to say so. */
	ok("nothing left to undo", undo_undo(t) == false);
	ok("but redo still walks forward", undo_redo(t));
	ok("and then stops", undo_redo(t) == false);

	vng_tabs_free();
	SDL_DestroyRenderer(vng_ren);
	SDL_DestroyWindow(vng_win);
	SDL_Quit();

	SDL_Log(fails ? "=== %d FAILED ===" : "=== all passed (%d failures) ===", fails);
	return fails ? 1 : 0;
}
