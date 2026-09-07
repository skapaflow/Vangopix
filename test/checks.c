/*
 * Headless checks: the undo stack (resize both ways, strokes, the dirty mark) and the
 * pencil (that a stroke joins its samples instead of coming out dotted).
 *
 * A hidden window is opened because a document owns a texture. Nothing here needs a hand
 * on the mouse - the pencil is driven with synthetic events, positioned through the real
 * view_world_to_screen so the test does not carry its own idea of where a pixel is.
 */
#include "vangopix.h"
#include "tabs.h"
#include "undo.h"
#include "tool.h"
#include "view.h"
#include "file.h"

/* Drives the tool the way core.c does: one synthetic event at the screen point that the
 * camera says a document pixel is under. */
static void mouse (VNG_TAB *t, Uint32 type, Uint8 btn, float px, float py)
{
	SDL_FPoint s = view_world_to_screen(t, px + 0.5f, py + 0.5f);

	SDL_Event e;
	SDL_zero(e);
	e.type = type;

	if (type == SDL_EVENT_MOUSE_MOTION) {
		e.motion.x = s.x;
		e.motion.y = s.y;
	} else {
		e.button.button = btn;
		e.button.x = s.x;
		e.button.y = s.y;
	}
	tool_event(&e, t);
}

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

	/* ---- the pencil ---- */
	VNG_TAB *p = vng_tab_new(16, 16);
	if (!p) { SDL_Log("no second tab"); return 2; }

	view_sheet_rect(p);            /* frames the camera, as the first draw would */
	undo_mark_saved(p);

	Uint32 blank[16 * 16];
	SDL_memcpy(blank, p->pixels, sizeof blank);

	/* One press and ONE motion event jumping five pixels away. A tool that painted only
	 * where the events landed would leave two dots and four holes. */
	mouse(p, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT,  2, 2);
	mouse(p, SDL_EVENT_MOUSE_MOTION,      0,                7, 7);
	mouse(p, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT,  7, 7);

	int gaps = 0;
	for (int i = 2; i <= 7; i++)
		if (p->pixels[i * 16 + i] != 0xFF000000u) gaps++;
	ok("THE STROKE JOINED ITS SAMPLES (no dotted line)", gaps == 0);
	ok("and did not paint beside the line", p->pixels[2 * 16 + 3] == blank[2 * 16 + 3]);
	ok("dirty after drawing", p->dirty == true);

	ok("one stroke is one undo step", undo_undo(p));
	ok("the whole line came back out", SDL_memcmp(p->pixels, blank, sizeof blank) == 0);
	ok("clean again", p->dirty == false);
	undo_redo(p);

	/* Right button rubs out to TRANSPARENT, not to white: in a program that keeps alpha,
	 * white is a colour somebody chose. */
	mouse(p, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 4, 4);
	mouse(p, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_RIGHT, 4, 4);
	ok("right button erases to transparent", p->pixels[4 * 16 + 4] == 0x00000000u);

	/* A press that starts off the paper is not a drawing gesture, and must fall through
	 * to whatever else may want it. */
	SDL_FPoint off = view_world_to_screen(p, -4.0f, -4.0f);
	SDL_Event e;
	SDL_zero(e);
	e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	e.button.button = SDL_BUTTON_LEFT;
	e.button.x = off.x;
	e.button.y = off.y;
	ok("a press outside the sheet is not consumed", tool_event(&e, p) == false);

	/* ---- what a save dialog's answer means ----
	 *
	 * Filter 0 is PNG and filter 1 is "jpg;jpeg" in file.c's list. -1 is a platform that
	 * did not report which one was showing.
	 */
	{
		char out[256];
		#define EXT(path, filter) (file_with_extension(out, sizeof out, path, filter), out)

		ok("a bare name takes the extension from the filter",
		   SDL_strcmp(EXT("C:/art/dragon", 0), "C:/art/dragon.png") == 0);
		ok("a jpeg filter writes the FIRST of jpg;jpeg",
		   SDL_strcmp(EXT("C:/art/dragon", 1), "C:/art/dragon.jpg") == 0);
		ok("what was typed wins over the dropdown",
		   SDL_strcmp(EXT("C:/art/dragon.webp", 0), "C:/art/dragon.webp") == 0);
		ok("no filter reported falls back to png",
		   SDL_strcmp(EXT("C:/art/dragon", -1), "C:/art/dragon.png") == 0);
		ok("a dot in a DIRECTORY is not an extension",
		   SDL_strcmp(EXT("C:/my.sprites/dragon", 0), "C:/my.sprites/dragon.png") == 0);
		ok("a version number is not an extension either",
		   SDL_strcmp(EXT("C:/art/dragon v1.2", 0), "C:/art/dragon v1.2.png") == 0);
		ok("a trailing dot does not become two",
		   SDL_strcmp(EXT("C:/art/dragon.", 0), "C:/art/dragon.png") == 0);
		ok("the All files entry has no opinion, so png",
		   SDL_strcmp(EXT("C:/art/dragon", 6), "C:/art/dragon.png") == 0);

		#undef EXT
	}

	vng_tabs_free();
	SDL_DestroyRenderer(vng_ren);
	SDL_DestroyWindow(vng_win);
	SDL_Quit();

	SDL_Log(fails ? "=== %d FAILED ===" : "=== all passed (%d failures) ===", fails);
	return fails ? 1 : 0;
}
