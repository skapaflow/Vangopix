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

/* A bare key press, the way core.c hands one to the tool. */
static bool key (VNG_TAB *t, SDL_Keycode k, SDL_Keymod mod)
{
	SDL_Event e;
	SDL_zero(e);
	e.type     = SDL_EVENT_KEY_DOWN;
	e.key.key  = k;
	e.key.mod  = mod;
	return tool_event(&e, t);
}

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

	/* ---- the eyedropper, and the byte order of what it reports ----
	 *
	 * The document is 0xAARRGGBB and the readout is RRGGBBAA. Those two orders being
	 * different is precisely how the checkerboard once came out red, so the conversion is
	 * pinned here rather than trusted.
	 */
	{
		char hex[16];

		tool_hex(0xFF4080FFu, hex, sizeof hex);
		ok("ARGB 0xFF4080FF reads as RRGGBBAA 4080FFFF",
		   SDL_strcmp(hex, "4080FFFF") == 0);

		tool_hex(0x00000000u, hex, sizeof hex);
		ok("nothing reads as 00000000", SDL_strcmp(hex, "00000000") == 0);

		tool_hex(0x80FF0000u, hex, sizeof hex);
		ok("half transparent red keeps its alpha last",
		   SDL_strcmp(hex, "FF000080") == 0);

		/* The CTRL branch of tool_event cannot be driven from here - keys_mods reads the
		 * real keyboard - so what is checked is tool_pick, which is where the absorbing
		 * actually happens, and the two slots either side of it. */
		ok("colour 2 starts as nothing, which is what makes the right button an eraser",
		   tool_colour(1) == 0x00000000u);

		p->pixels[3 * 16 + 5] = 0xFF123456u;
		tool_pick(p, 5, 3, 0);
		ok("a pick fills the slot it was given", tool_colour(0) == 0xFF123456u);
		ok("and leaves the other one alone",    tool_colour(1) == 0x00000000u);

		Uint32 held = tool_colour(0);
		tool_pick(p, 99, 99, 0);
		ok("a pick outside the sheet changes nothing", tool_colour(0) == held);

		/* The colour outlives a stroke: it belongs to the pencil, not to the drag. */
		mouse(p, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 9, 9);
		mouse(p, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 9, 9);
		ok("the LEFT button draws colour 1", p->pixels[9 * 16 + 9] == 0xFF123456u);
		ok("and the colour survives the stroke", tool_colour(0) == 0xFF123456u);

		/* Colour 2 is still nothing, so the right button rubs out. */
		mouse(p, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 9, 9);
		mouse(p, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_RIGHT, 9, 9);
		ok("the RIGHT button draws colour 2, which is nothing",
		   p->pixels[9 * 16 + 9] == 0x00000000u);

		/* Put something in slot 2 and the right button stops being an eraser - the eraser
		 * was never a tool, only a colour that happened to be absent. */
		p->pixels[2 * 16 + 2] = 0xFFABCDEFu;
		tool_pick(p, 2, 2, 1);
		mouse(p, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 9, 9);
		mouse(p, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_RIGHT, 9, 9);
		ok("a filled slot 2 makes the right button draw with it",
		   p->pixels[9 * 16 + 9] == 0xFFABCDEFu);
		ok("colour 1 was not touched by any of that",
		   tool_colour(0) == 0xFF123456u);
	}

	/* ---- the eight tools ---- */
	{
		ok("Q selects the pencil", key(p, SDLK_Q, SDL_KMOD_NONE) && tool_current() == T_PENCIL);
		ok("W the line",           key(p, SDLK_W, SDL_KMOD_NONE) && tool_current() == T_LINE);
		ok("E the rect",           key(p, SDLK_E, SDL_KMOD_NONE) && tool_current() == T_RECT);
		ok("R the ellipse",        key(p, SDLK_R, SDL_KMOD_NONE) && tool_current() == T_ELLIPSE);
		ok("A the eraser",         key(p, SDLK_A, SDL_KMOD_NONE) && tool_current() == T_ERASER);
		ok("S the bucket",         key(p, SDLK_S, SDL_KMOD_NONE) && tool_current() == T_BUCKET);
		ok("D the spray",          key(p, SDLK_D, SDL_KMOD_NONE) && tool_current() == T_SPRAY);
		ok("F change-colours",     key(p, SDLK_F, SDL_KMOD_NONE) && tool_current() == T_CHANGE);

		/* CTRL+S is the save, not the bucket. A tool key only counts bare. */
		key(p, SDLK_Q, SDL_KMOD_NONE);
		ok("CTRL+S is not the bucket",
		   key(p, SDLK_S, SDL_KMOD_CTRL) == false && tool_current() == T_PENCIL);
		ok("SHIFT+A is not the eraser either",
		   key(p, SDLK_A, SDL_KMOD_SHIFT) == false && tool_current() == T_PENCIL);
	}

	/* ---- a shape is REDRAWN from its anchor, never accumulated ---- */
	{
		VNG_TAB *c = vng_tab_new(24, 24);
		view_sheet_rect(c);
		undo_mark_saved(c);

		Uint32 blank[24 * 24];
		SDL_memcpy(blank, c->pixels, sizeof blank);

		key(c, SDLK_W, SDL_KMOD_NONE);   /* line */

		/* Press at (2,2), drag out to (2,20), then back to (2,6) and release. The middle
		 * position must leave no trace: only the last line drawn is the line. */
		mouse(c, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 2, 2);
		mouse(c, SDL_EVENT_MOUSE_MOTION,      0,               2, 20);
		mouse(c, SDL_EVENT_MOUSE_MOTION,      0,               2, 6);
		mouse(c, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 2, 6);

		int drawn = 0;
		for (int i = 2; i <= 6; i++) if (c->pixels[i * 24 + 2] != blank[0]) drawn++;
		ok("the line landed where the drag ENDED", drawn == 5);
		ok("AND NOWHERE THE DRAG PASSED THROUGH",
		   c->pixels[12 * 24 + 2] == blank[0] && c->pixels[20 * 24 + 2] == blank[0]);
		ok("the whole drag is one undo",
		   undo_undo(c) && SDL_memcmp(c->pixels, blank, sizeof blank) == 0);

		/* The rectangle is an outline: its corners are set and its middle is not. */
		key(c, SDLK_E, SDL_KMOD_NONE);
		mouse(c, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 4, 4);
		mouse(c, SDL_EVENT_MOUSE_MOTION,      0,               10, 10);
		mouse(c, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 10, 10);
		ok("the rect drew its four corners",
		   c->pixels[4 * 24 + 4]  != blank[0] && c->pixels[4 * 24 + 10] != blank[0] &&
		   c->pixels[10 * 24 + 4] != blank[0] && c->pixels[10 * 24 + 10] != blank[0]);
		ok("and left its middle alone", c->pixels[7 * 24 + 7] == blank[0]);
		undo_undo(c);

		/* The bucket fills the region it was dropped in, and only that region. */
		key(c, SDLK_Q, SDL_KMOD_NONE);
		for (int i = 0; i < 24; i++) c->pixels[12 * 24 + i] = 0xFF00FF00u;  /* a wall */

		key(c, SDLK_S, SDL_KMOD_NONE);
		mouse(c, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 3, 3);
		mouse(c, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 3, 3);

		ok("the bucket filled above the wall",  c->pixels[0 * 24 + 0]  == tool_colour(0));
		ok("did not cross it",                 c->pixels[20 * 24 + 0] == blank[0]);
		ok("and did not paint the wall",       c->pixels[12 * 24 + 5] == 0xFF00FF00u);
		ok("one bucket is one undo",           undo_undo(c));
		ok("which put the region back",        c->pixels[0] == blank[0]);
	}

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
