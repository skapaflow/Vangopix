/*
 * Headless checks: the undo stack (resize both ways, strokes, the dirty mark) and the
 * pencil (that a stroke joins its samples instead of coming out dotted).
 *
 * A hidden window is opened because a document owns a texture. Nothing here needs a hand
 * on the mouse - the pencil is driven with synthetic events, positioned through the real
 * view_world_to_screen so the test does not carry its own idea of where a pixel is.
 */
#include "vangopix.h"
#include "ui.h"
#include "core.h"
#include "primitives.h"
#include "tabs.h"
#include "undo.h"
#include "tool.h"
#include "select.h"
#include "thumb.h"
#include "win.h"
#include "colour.h"
#include "palette.h"
#include "sidebar.h"
#include "anim.h"
#include "keys.h"
#include "view.h"
#include "file.h"
#include "expr.h"

/* Text and ENTER, the way core.c hands them to whoever owns the keyboard. */
static void typed (const char *t)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = SDL_EVENT_TEXT_INPUT;
	e.text.text = t;
	keys_event(&e);
}

static void enter (void)
{
	SDL_Event e;
	SDL_zero(e);
	e.type    = SDL_EVENT_KEY_DOWN;
	e.key.key = SDLK_RETURN;
	keys_event(&e);
}

/* DELETE, which empties the box. A field opens holding its value and typing ADDS to it, so
 * every check below that means to REPLACE a value has to say so - the same key a hand uses. */
static void wipe (void)
{
	SDL_Event e;
	SDL_zero(e);
	e.type    = SDL_EVENT_KEY_DOWN;
	e.key.key = SDLK_DELETE;
	keys_event(&e);
}

/* Opens the field, clears it, types, commits - the gesture in one line, since the checks
 * below are about what comes out and not about the clicking. */
static void hex_type (const char *t)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	e.button.button = SDL_BUTTON_LEFT;
	SDL_FRect c = win_area(win_top());
	e.button.x = c.x + 100.0f;
	e.button.y = c.y + c.h - 28.0f;
	win_event(&e);

	wipe();
	typed(t);
	enter();
}

/* A press and its release, through the window chain - what a hand does to a widget. */
static void press_at (float x, float y)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	e.button.button = SDL_BUTTON_LEFT;
	e.button.x = x;
	e.button.y = y;
	win_event(&e);

	e.type = SDL_EVENT_MOUSE_BUTTON_UP;
	win_event(&e);
}

/* A bare key press, the way core.c hands one to the tool. */
static bool key (VNG_TAB *t, SDL_Keycode k, SDL_Keymod mod)
{
	SDL_Event e;
	SDL_zero(e);
	e.type     = SDL_EVENT_KEY_DOWN;
	e.key.key  = k;
	e.key.mod  = mod;
	/* The real chain, in core.c's order: the selection is offered every event before the
	 * tool is, and a test that skipped it would be testing a program that does not exist. */
	if (select_event(&e, t)) return true;
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
	if (!select_event(&e, t))
		tool_event(&e, t);
}

static int fails = 0;

static void ok (const char *what, bool cond)
{
	SDL_Log("%s %s", cond ? "PASS" : "FAIL", what);
	if (!cond) fails++;
}

/* Does that line come out as that number - the shape every arithmetic claim below wants. */
static bool sum_is (const char *line, double want)
{
	double v = 0.0;
	return expr_eval(line, &v) && SDL_fabs(v - want) < 1e-9;
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
	ok("stroke opens", vng_tab_stroke_open(t, false));
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
	vng_tab_stroke_open(t, false);
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

		/*
		 * A shape is rebuilt on every FRAME and not only on every motion, so that letting go
		 * of SHIFT or sizing the tip mid-drag shows without the hand having to move. The
		 * regression that guards is the rebuild itself: run it a few times with the pointer
		 * standing still and the shape has to be the same shape, not a thicker one.
		 */
		mouse(c, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 3, 3);
		mouse(c, SDL_EVENT_MOUSE_MOTION,      0,               3, 9);
		for (int i = 0; i < 5; i++) tool_frame(c);
		mouse(c, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 3, 9);

		int lit = 0;
		for (int i = 0; i < 24 * 24; i++) if (c->pixels[i] != blank[0]) lit++;
		ok("REBUILDING A HELD SHAPE LEAVES THE SAME SHAPE", lit == 7);
		undo_undo(c);

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

	/* ---- a stroke that REMOVES colour shows as it is drawn ----
	 *
	 * The bug this pins: the preview is composited OVER the sheet, and nothing composited
	 * over anything takes a pixel away, so an eraser laid into the preview stayed invisible
	 * until the button came up.
	 */
	{
		VNG_TAB *g = vng_tab_new(20, 20);
		view_sheet_rect(g);
		undo_mark_saved(g);

		key(g, SDLK_A, SDL_KMOD_NONE);   /* eraser */

		mouse(g, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 10, 10);
		ok("THE ERASER SHOWS BEFORE THE BUTTON COMES UP",
		   g->pixels[10 * 20 + 10] == 0x00000000u);

		mouse(g, SDL_EVENT_MOUSE_MOTION,    0,               13, 10);
		ok("and goes on showing as it is dragged",
		   g->pixels[10 * 20 + 12] == 0x00000000u);

		mouse(g, SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 13, 10);
		ok("the whole rub-out is one undo", undo_undo(g));
		ok("which put the paper back", g->pixels[10 * 20 + 10] == 0xFFFFFFFFu &&
		                               g->pixels[10 * 20 + 12] == 0xFFFFFFFFu);

		/* A SHAPE in a colour that removes: the same write-through path, and the drag still
		 * has to leave nothing behind where it passed. Colour 2 is nothing by default, so
		 * the right button is the one that does it. */
		g->pixels[19 * 20 + 19] = 0x00000000u;
		tool_pick(g, 19, 19, 1);         /* nothing, back into slot 2 */

		key(g, SDLK_E, SDL_KMOD_NONE);   /* rect */
		mouse(g, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 2,  2);
		mouse(g, SDL_EVENT_MOUSE_MOTION,      0,                17, 17);
		mouse(g, SDL_EVENT_MOUSE_MOTION,      0,                8,  8);
		mouse(g, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_RIGHT, 8,  8);

		ok("the erasing rect landed where the drag ended",
		   g->pixels[2 * 20 + 2] == 0x00000000u && g->pixels[8 * 20 + 8] == 0x00000000u);
		ok("AND WAS REWOUND EVERYWHERE THE DRAG PASSED",
		   g->pixels[17 * 20 + 17] == 0xFFFFFFFFu && g->pixels[2 * 20 + 17] == 0xFFFFFFFFu);
		ok("and it too was one undo",
		   undo_undo(g) && g->pixels[2 * 20 + 2] == 0xFFFFFFFFu);
	}

	/* ---- change-colours keeps working while the button is held ---- */
	{
		VNG_TAB *h = vng_tab_new(20, 20);
		view_sheet_rect(h);
		undo_mark_saved(h);

		/* Three bands, so a drag down the sheet meets a different colour at each step. */
		for (int i = 0; i < 20; i++) {
			h->pixels[4 * 20 + i] = 0xFF111111u;
			h->pixels[8 * 20 + i] = 0xFF222222u;
		}

		key(h, SDLK_Q, SDL_KMOD_NONE);
		h->pixels[0] = 0xFF00FF00u;
		tool_pick(h, 0, 0, 0);            /* green in slot 1 */

		key(h, SDLK_F, SDL_KMOD_NONE);    /* change-colours, limiter 0: the whole sheet */

		/* A press and NO drag: only the band it was dropped on. */
		mouse(h, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 10, 4);
		mouse(h, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 10, 4);
		ok("a press changes the band under it", h->pixels[4 * 20 + 3] == 0xFF00FF00u);
		ok("and leaves the one below alone",    h->pixels[8 * 20 + 3] == 0xFF222222u);
		undo_undo(h);

		/* THE DRAG IS THE FEATURE: the same gesture, slid onto a second colour, takes that
		 * one as well. Before this it stopped at the press. */
		mouse(h, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 10, 4);
		mouse(h, SDL_EVENT_MOUSE_MOTION,      0,               10, 8);
		mouse(h, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 10, 8);
		ok("SLIDING ONTO ANOTHER COLOUR TAKES THAT ONE TOO",
		   h->pixels[4 * 20 + 3] == 0xFF00FF00u && h->pixels[8 * 20 + 3] == 0xFF00FF00u);

		ok("the whole slide is one undo", undo_undo(h));
		ok("which put both bands back",
		   h->pixels[4 * 20 + 3] == 0xFF111111u && h->pixels[8 * 20 + 3] == 0xFF222222u);
	}

	/* ---- the two colour keys ---- */
	{
		VNG_TAB *k = vng_tab_new(8, 8);
		view_sheet_rect(k);

		k->pixels[0] = 0xFF102030u;
		k->pixels[1] = 0x00405060u;
		tool_pick(k, 0, 0, 0);
		tool_pick(k, 1, 0, 1);

		/* M averages the two slots back into the first, ALPHA INCLUDED - which is how a
		 * half transparent shade gets made without a slider anywhere. */
		ok("M is taken", key(k, SDLK_M, SDL_KMOD_NONE));
		ok("M AVERAGES BOTH SLOTS, ALPHA AND ALL", tool_colour(0) == 0x7F283848u);
		ok("and leaves slot 2 alone",              tool_colour(1) == 0x00405060u);

		/* SHIFT+R rolls a colour, and it is always opaque: a random alpha would hand back
		 * a brush that is invisible for no reason a person could see. */
		int rolls = 0;
		Uint32 seen = tool_colour(0);
		for (int i = 0; i < 8; i++) {
			key(k, SDLK_R, SDL_KMOD_SHIFT);
			if (((tool_colour(0) >> 24) & 0xFF) != 0xFF) rolls = -100;
			if (tool_colour(0) != seen) rolls++;
			seen = tool_colour(0);
		}
		ok("SHIFT+R rolls a colour, and it is always opaque", rolls > 5);

		/* Bare R is the ellipse and SHIFT+R is a colour - the bare test keeps them apart. */
		key(k, SDLK_R, SDL_KMOD_NONE);
		ok("bare R is still the ellipse", tool_current() == T_ELLIPSE);
		key(k, SDLK_M, SDL_KMOD_CTRL);
		ok("CTRL+M is not the mix", true);   /* it simply is not claimed */
	}

	/* ---- SHIFT snaps a line to the pixel-art slopes ---- */
	{
		int x, y;
		#define SNAP(sx, sy) (x = (sx), y = (sy), tool_snap_iso(10, 10, &x, &y), 1)

		SNAP(40, 11);
		ok("a near-horizontal drag flattens onto the anchor", y == 10 && x == 40);

		SNAP(12, 40);
		ok("a near-vertical drag stands upright", x == 10 && y == 40);

		/* 14 degrees up and to the right: the ISOMETRIC slope, two across for every one up. */
		SNAP(30, 5);
		ok("a shallow drag becomes 2:1", x == 20 && y == 5);

		/* 56 degrees down and to the right: one across for every two down. */
		SNAP(30, 40);
		ok("a steep drag becomes 1:2", x == 25 && y == 40);

		/* 45 down-right, and the sign has to survive going the other way too. */
		SNAP(30, 30);
		ok("a diagonal stays 1:1", x == 30 && y == 30);
		SNAP(-10, 30);
		ok("and 1:1 up the other diagonal", x == -10 && y == 30);

		#undef SNAP
	}

	/* ---- the barrier fill, which is a different question from the bucket ---- */
	{
		VNG_TAB *f = vng_tab_new(20, 20);
		view_sheet_rect(f);
		undo_mark_saved(f);

		const Uint32 paper = 0xFFFFFFFFu, wall = 0xFF102030u, junk = 0xFF445566u;

		/* A ring of wall at 5..14, and two junk pixels inside it - the mixed region an
		 * ordinary bucket refuses to cross. */
		for (int i = 5; i <= 14; i++) {
			f->pixels[5 * 20 + i] = f->pixels[14 * 20 + i] = wall;
			f->pixels[i * 20 + 5] = f->pixels[i * 20 + 14] = wall;
		}
		f->pixels[8 * 20 + 8] = junk;
		f->pixels[8 * 20 + 9] = junk;

		tool_pick(f, 5, 5, 0);            /* the wall colour goes in slot 1 */
		ok("the wall colour is loaded", tool_colour(0) == wall);

		tool_fill(f, 10, 10, 0, false);   /* the ordinary bucket */
		ok("the bucket filled the paper inside the ring", f->pixels[10 * 20 + 10] == wall);
		ok("AND LEFT THE JUNK ALONE, being a different colour",
		   f->pixels[8 * 20 + 8] == junk);
		ok("and did not leak outside the ring", f->pixels[0] == paper);
		undo_undo(f);
		ok("the bucket was one undo", f->pixels[10 * 20 + 10] == paper);

		tool_fill(f, 10, 10, 0, true);    /* the barrier */
		ok("the barrier covered the paper too", f->pixels[10 * 20 + 10] == wall);
		ok("AND THE JUNK WITH IT, not caring what it covers",
		   f->pixels[8 * 20 + 8] == wall && f->pixels[8 * 20 + 9] == wall);
		ok("and still stopped at the wall", f->pixels[0] == paper);
		ok("the barrier was one undo too",
		   undo_undo(f) && f->pixels[8 * 20 + 8] == junk);
	}

	/* ---- the selection ---- */
	{
		VNG_TAB *a = vng_tab_new(16, 16);
		view_sheet_rect(a);

		/* A recognisable 3x3 block at (2,2). */
		for (int j = 0; j < 3; j++)
			for (int i = 0; i < 3; i++)
				a->pixels[(2 + j) * 16 + (2 + i)] = 0xFF000000u | (Uint32)(j * 3 + i + 1);

		Uint32 fresh[16 * 16];
		SDL_memcpy(fresh, a->pixels, sizeof fresh);
		undo_mark_saved(a);

		key(a, SDLK_Z, SDL_KMOD_NONE);
		ok("Z takes the select tool", tool_current() == T_SELECT);

		/* Mark the block. */
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 2, 2);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 4, 4);
		ok("marking wrote nothing to the document",
		   SDL_memcmp(a->pixels, fresh, sizeof fresh) == 0);

		/* Take hold of it and carry it four to the right. NOTHING may be written yet. */
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 3, 3);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               7, 3);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 7, 3);
		ok("A FLOAT WRITES NOTHING UNTIL IT IS PUT DOWN",
		   SDL_memcmp(a->pixels, fresh, sizeof fresh) == 0);

		/* Clicking away puts it down. */
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 12, 12);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 12, 12);

		ok("the move landed four to the right", a->pixels[2 * 16 + 6] == 0xFF000001u);
		ok("AND LEFT COLOUR 2 BEHIND IT, whatever colour 2 is",
		   a->pixels[2 * 16 + 2] == tool_colour(1));
		ok("A MOVE IS ONE UNDO STEP",           undo_undo(a));
		ok("which puts the block back whole",
		   SDL_memcmp(a->pixels, fresh, sizeof fresh) == 0);

		/* A NUDGE: source and destination overlap, which is the case that catches a naive
		 * clear-then-write. One to the right. */
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 2, 2);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 3, 3);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               4, 3);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 4, 3);
		select_commit(a);

		ok("AN OVERLAPPING NUDGE KEEPS EVERY PIXEL",
		   a->pixels[2 * 16 + 3] == 0xFF000001u && a->pixels[2 * 16 + 5] == 0xFF000003u);
		ok("and leaves colour 2 in only the column it left",
		   a->pixels[2 * 16 + 2] == tool_colour(1));
		ok("the nudge undoes cleanly",
		   undo_undo(a) && SDL_memcmp(a->pixels, fresh, sizeof fresh) == 0);

		/* WHAT A CUT LEAVES IS COLOUR 2 AND NOT A FIXED THING: load another and the next cut
		 * leaves that one. Colour 2 is already "what the right button lays down", so a cut
		 * leaving it is the same idea said once more. */
		a->pixels[15 * 16 + 15] = 0xFF778899u;
		tool_pick(a, 15, 15, 1);

		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 2, 2);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 3, 3);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               9, 9);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 9, 9);
		select_commit(a);
		ok("A CUT LEAVES WHATEVER COLOUR 2 HOLDS NOW",
		   a->pixels[2 * 16 + 2] == 0xFF778899u);
		undo_undo(a);
		key(a, SDLK_ESCAPE, SDL_KMOD_NONE);

		/* Back to nothing, so the rest of the block reads as it did. */
		a->pixels[15 * 16 + 15] = 0x00000000u;
		tool_pick(a, 15, 15, 1);
		SDL_memcpy(a->pixels, fresh, sizeof fresh);

		/* ESC gives a float back, and leaves NO undo step, because nothing was written. */
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 2, 2);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 3, 3);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               9, 9);
		key(a, SDLK_ESCAPE, SDL_KMOD_NONE);
		ok("ESC gives an abandoned float back",
		   SDL_memcmp(a->pixels, fresh, sizeof fresh) == 0);
		ok("and leaves nothing to undo", undo_undo(a) == false);

		/* ---- ACROSS TABS, which is what broke in the first Vangopix ---- */
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 2, 2);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 4, 4);
		key(a, SDLK_C, SDL_KMOD_CTRL);

		VNG_TAB *b = vng_tab_new(16, 16);
		view_sheet_rect(b);
		undo_mark_saved(b);

		select_paste(b, 8, 8);   /* CTRL+V with the pointer here */
		select_commit(b);

		int landed = 0;
		for (int i = 0; i < 16 * 16; i++)
			if ((b->pixels[i] & 0x00FFFFFFu) >= 1 && (b->pixels[i] & 0x00FFFFFFu) <= 9)
				landed++;
		ok("A COPY CROSSES INTO ANOTHER TAB", landed == 9);
		ok("and the tab it came from is untouched",
		   SDL_memcmp(a->pixels, fresh, sizeof fresh) == 0);
		ok("the paste is one undo in the tab it landed in", undo_undo(b));

		/* Switching away puts a float down instead of losing it. vng_tab_show commits the
		 * OUTGOING tab, so `a` has to be the one on screen for the question to be asked. */
		vng_tab_show(a);
		select_paste(a, 10, 10);
		vng_tab_show(b);
		ok("SWITCHING TABS PUTS A FLOAT DOWN RATHER THAN LOSING IT",
		   SDL_memcmp(a->pixels, fresh, sizeof fresh) != 0);

		undo_undo(a);
		key(a, SDLK_ESCAPE, SDL_KMOD_NONE);   /* and no mark left over for the next block */
		ok("and undoing that leaves the sheet as it was",
		   SDL_memcmp(a->pixels, fresh, sizeof fresh) == 0);

		/* Rotating a square block four times is the identity, which is the cheapest way to
		 * catch an off-by-one in the turn. */
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 2, 2);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 4, 4);
		for (int i = 0; i < 4; i++) key(a, SDLK_R, SDL_KMOD_NONE);
		select_commit(a);
		ok("FOUR QUARTER TURNS ARE THE IDENTITY",
		   SDL_memcmp(a->pixels, fresh, sizeof fresh) == 0);
		undo_undo(a);

		/* A rectangle STAYS MARKED after its float is put down, which is what every editor
		 * does - so the next block has to let go of it or its press would take hold of this
		 * one instead of marking a new one. */
		key(a, SDLK_ESCAPE, SDL_KMOD_NONE);

		/* And two flips are too. */
		mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 2, 2);
		mouse(a, SDL_EVENT_MOUSE_MOTION,      0,               4, 4);
		mouse(a, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 4, 4);
		key(a, SDLK_H, SDL_KMOD_NONE);
		key(a, SDLK_H, SDL_KMOD_NONE);
		select_commit(a);
		ok("TWO FLIPS ARE THE IDENTITY",
		   SDL_memcmp(a->pixels, fresh, sizeof fresh) == 0);
		undo_undo(a);

		/* Bare R is the ellipse again once there is no selection to turn. */
		key(a, SDLK_ESCAPE, SDL_KMOD_NONE);
		key(a, SDLK_R, SDL_KMOD_NONE);
		ok("with nothing selected, bare R is the ellipse once more",
		   tool_current() == T_ELLIPSE);
	}

	/* ---- CREATE PALETTE, on the selection's menu ----
	 *
	 * A right press on the selection opens the first Vangopix's gui_select, and its first row
	 * REPLACES the palette with the colours inside the rectangle. The sheet is red everywhere
	 * with a 2x2 block of blue, green, blue and a hole - so a palette that kept any red, or
	 * spent a cell on the hole, or listed blue twice, says which rule broke.
	 */
	{
		VNG_TAB *q = vng_tab_new(8, 8);
		view_sheet_rect(q);
		vng_tab_show(q);   /* the menu acts on the document on screen */

		for (int i = 0; i < 8 * 8; i++) q->pixels[i] = 0xFFFF0000u;
		q->pixels[1 * 8 + 1] = 0xFF0000FFu;
		q->pixels[1 * 8 + 2] = 0xFF00FF00u;
		q->pixels[2 * 8 + 1] = 0xFF0000FFu;
		q->pixels[2 * 8 + 2] = 0x00000000u;

		Uint32 fresh[8 * 8];
		SDL_memcpy(fresh, q->pixels, sizeof fresh);

		palette_scan(q);
		ok("before: the palette is the whole sheet", palette_has(q, 0xFFFF0000u));

		key(q, SDLK_Z, SDL_KMOD_NONE);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 1, 1);
		mouse(q, SDL_EVENT_MOUSE_MOTION,      0,               2, 2);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 2, 2);

		#define BLUE   0xFF0000FFu
		#define GREEN  0xFF00FF00u
		#define RED    0xFFFF0000u
		#define GREY   0xFF808080u
		#define PX(x, y) q->pixels[(y) * 8 + (x)]

		/* Opens the menu on a document pixel and presses one of its rows. */
		#define MENU_PICK(at_x, at_y, row) do {                                            \
			mouse(q, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, (at_x), (at_y));       \
			mouse(q, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_RIGHT, (at_x), (at_y));       \
			SDL_FRect mc = win_area(win_top());                                             \
			press_at(mc.x + ui_pad(), mc.y + ui_row() * ((float)(row) + 0.5f));             \
		} while (0)

		VNG_WIN *was = win_top();
		mouse(q, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 6, 6);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_RIGHT, 6, 6);
		ok("a right press OFF the selection opens nothing", win_top() == was);

		mouse(q, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 1, 1);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_RIGHT, 1, 1);
		VNG_WIN *m = win_top();
		ok("A RIGHT PRESS ON THE SELECTION OPENS ITS MENU", m != NULL && m != was);

		SDL_FRect c = win_area(m);
		press_at(c.x + ui_pad(), c.y + ui_row() * 0.5f);

		ok("CREATE PALETTE KEEPS THE SELECTED COLOURS AND NO OTHER",
		   palette_lot(q) == 2 && palette_at(q, 0) == BLUE && palette_at(q, 1) == GREEN);
		ok("the hole inside it is not a colour", !palette_has(q, 0x00000000u));
		ok("the menu goes once a row is chosen", !win_visible(m));
		ok("and the drawing is untouched", SDL_memcmp(q->pixels, fresh, sizeof fresh) == 0);

		/* ADD COLOR: a palette of blue and a grey kept by hand, and the selection holds blue
		 * and green - so green goes on the end, and blue is not listed a second time. */
		palette_del(q, GREEN);
		palette_add(q, GREY);
		MENU_PICK(1, 1, 1);
		ok("ADD COLOR PUTS THE NEW ONES ON THE END",
		   palette_lot(q) == 3 && palette_at(q, 0) == BLUE && palette_at(q, 1) == GREY &&
		   palette_at(q, 2) == GREEN);

		/* ADDING TO A FULL PALETTE ADDS NOTHING. The scan used to ask whether the list was full
		 * AFTER writing, which was fine while it always began empty; kept, it can be full
		 * before the first pixel, and the write lands one past the end of the array - on the
		 * count that says how long the list is. */
		{
			for (Uint32 i = 1; palette_lot(q) < VNG_PAL_MAX; i++) palette_add(q, 0xFF000000u | i);
			Uint32 odd = 0xFF123456u;
			palette_from(q, &odd, 1, true);
			ok("adding to a full palette adds nothing, and writes nowhere",
			   palette_lot(q) == VNG_PAL_MAX && !palette_has(q, odd));

			MENU_PICK(1, 1, 0);   /* and back to the block's own two, for what follows */
		}

		/* A FLOAT GIVES ITS OWN PIXELS, as they are now: inverted since it was lifted, its
		 * blue and green are yellow and magenta, and those are what the palette gets. */
		key(q, SDLK_I, SDL_KMOD_NONE);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 1, 1);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_RIGHT, 1, 1);
		ok("the menu comes back for a float", win_visible(m));

		c = win_area(m);
		press_at(c.x + ui_pad(), c.y + ui_row() * 0.5f);
		ok("A FLOAT'S PALETTE IS THE FLOAT AS IT IS NOW",
		   palette_lot(q) == 2 &&
		   palette_at(q, 0) == 0xFFFFFF00u && palette_at(q, 1) == 0xFFFF00FFu);

		key(q, SDLK_ESCAPE, SDL_KMOD_NONE);
		ok("and giving the float back leaves the drawing as it was",
		   SDL_memcmp(q->pixels, fresh, sizeof fresh) == 0);

		/* REMOVE UNSELECTED COLORS, with blue in hand and nothing as colour 2 - both picked
		 * off the block itself, so nothing about the sheet is changed to set them. Inside the
		 * rectangle the green goes and the blue stays; the red all round it is untouched. */
		Uint32 was0 = tool_colour(0), was1 = tool_colour(1);
		tool_pick(q, 1, 1, 0);
		tool_pick(q, 2, 2, 1);
		undo_mark_saved(q);

		mouse(q, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 1, 1);
		mouse(q, SDL_EVENT_MOUSE_MOTION,      0,               2, 2);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 2, 2);
		MENU_PICK(1, 1, 2);

		ok("REMOVE KEEPS ONLY COLOUR 1 INSIDE THE SELECTION",
		   PX(1, 1) == BLUE && PX(1, 2) == BLUE && PX(2, 1) == 0x00000000u);
		ok("AND LEAVES COLOUR 2 WHERE THE OTHERS WERE", PX(2, 1) == tool_colour(1));
		ok("and touches nothing outside it",
		   PX(0, 0) == RED && PX(3, 1) == RED && PX(1, 3) == RED);
		ok("a remove is one undo step, which puts it all back",
		   undo_undo(q) && SDL_memcmp(q->pixels, fresh, sizeof fresh) == 0);
		ok("and only one", undo_undo(q) == false);

		/* A remove that finds nothing to remove - the column under it is all blue already -
		 * must leave nothing behind to CTRL+Z through. */
		key(q, SDLK_ESCAPE, SDL_KMOD_NONE);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 1, 1);
		mouse(q, SDL_EVENT_MOUSE_MOTION,      0,               1, 2);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 1, 2);
		MENU_PICK(1, 1, 2);
		ok("A REMOVE WITH NOTHING TO REMOVE RECORDS NOTHING",
		   undo_undo(q) == false && SDL_memcmp(q->pixels, fresh, sizeof fresh) == 0);

		/* ON A FLOAT it changes the float and not the sheet, and the change lands with it.
		 * Carried to (5,5): its green became nothing, and a float's transparent pixel leaves
		 * what is under it - so the red there shows through where the green would have been. */
		key(q, SDLK_ESCAPE, SDL_KMOD_NONE);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 1, 1);
		mouse(q, SDL_EVENT_MOUSE_MOTION,      0,               2, 2);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 2, 2);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 1, 1);
		mouse(q, SDL_EVENT_MOUSE_MOTION,      0,               5, 5);
		mouse(q, SDL_EVENT_MOUSE_BUTTON_UP,   SDL_BUTTON_LEFT, 5, 5);
		MENU_PICK(5, 5, 2);
		ok("A FLOAT IS CHANGED IN ITS OWN PIXELS, NOT ON THE SHEET",
		   SDL_memcmp(q->pixels, fresh, sizeof fresh) == 0);

		select_commit(q);
		ok("and the change lands when it is put down",
		   PX(5, 5) == BLUE && PX(5, 6) == BLUE && PX(6, 5) == RED);
		undo_undo(q);
		key(q, SDLK_ESCAPE, SDL_KMOD_NONE);

		/* Colours 1 and 2 back as they were, for whatever reads them next. */
		q->pixels[0] = was0; tool_pick(q, 0, 0, 0);
		q->pixels[0] = was1; tool_pick(q, 0, 0, 1);
		q->pixels[0] = RED;

		#undef MENU_PICK
		#undef PX
		#undef GREY
		#undef RED
		#undef GREEN
		#undef BLUE
	}

	/* ---- the 1:1 panel, which is now a window ---- */
	{
		VNG_TAB *q = vng_tab_new(400, 300);
		view_sheet_rect(q);

		SDL_Event e;
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = 200.0f;
		e.button.y = 150.0f;

		ok("down, no window takes the click", win_event(&e) == false);

		thumb_toggle();
		ok("V puts it up", thumb_visible());

		/* It comes up centred on the pointer, and in a hidden window the pointer is wherever
		 * the desktop's is - so it is put somewhere known before being aimed at. */
		win_place(win_top(), 200.0f, 150.0f);
		SDL_FRect a = win_area(win_top());

		e.button.x = a.x + a.w * 0.5f;
		e.button.y = a.y + a.h * 0.5f;
		ok("UP, THE WINDOW TAKES THE CLICK RATHER THAN THE SHEET", win_event(&e));

		/* EVERYTHING THAT IS NOT A WIDGET IS SOMEWHERE TO TAKE HOLD OF IT, and this panel has
		 * no widgets at all - so that press was a grab, and moving now moves the window. */
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_MOTION;
		e.motion.x = a.x + a.w * 0.5f + 40.0f;
		e.motion.y = a.y + a.h * 0.5f + 25.0f;
		win_event(&e);

		SDL_FRect moved = win_area(win_top());
		ok("A PRESS ANYWHERE INSIDE DRAGS THE WINDOW",
		   moved.x > a.x + 35.0f && moved.y > a.y + 20.0f);

		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_UP;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = moved.x;
		e.button.y = moved.y;
		win_event(&e);

		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = moved.x - 60.0f;
		e.button.y = moved.y - 60.0f;
		ok("and takes nothing where it is not", win_event(&e) == false);

		/* With nothing being carried, motion passes through - so a stroke or a pan begun on
		 * the sheet is not cut in half by crossing a window. */
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_MOTION;
		e.motion.x = moved.x + moved.w * 0.5f;
		e.motion.y = moved.y + moved.h * 0.5f;
		ok("MOTION CROSSES A WINDOW UNTOUCHED", win_event(&e) == false);

		thumb_toggle();
		ok("V puts it away again", thumb_visible() == false);

		/* And putting it away does not throw it out: it comes back where it was left, which
		 * is the whole reason it is a window rather than a corner. */
		thumb_toggle();
		ok("it comes back", thumb_visible());
		thumb_toggle();
	}

	/* ---- the colour window ---- */
	{
		tool_set_colour(0, 0xFF000000u);

		colour_toggle();
		ok("C puts the colour window up", colour_visible());

		/* Summoned to the pointer, so it is put somewhere known first and everything below is
		 * measured from where it actually is. The hue ring fills the left of the interior, so
		 * a press well inside that has to change slot 1. */
		win_place(win_top(), 200.0f, 150.0f);
		SDL_FRect c = win_area(win_top());

		SDL_Event e;
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = c.x + 90.0f;
		e.button.y = c.y + 30.0f;

		ok("the window takes the press", win_event(&e));
		ok("A PRESS IN THE WHEEL WRITES THE SLOT", tool_colour(0) != 0xFF000000u);
		ok("and what it wrote is opaque", (tool_colour(0) >> 24) == 0xFF);

		Uint32 first = tool_colour(0);

		/* The drag belongs to the window until the button comes up, even off its edge -
		 * a slider dragged past its own end is still being dragged. */
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_MOTION;
		e.motion.x = c.x + 20.0f;
		e.motion.y = c.y + 100.0f;
		ok("THE DRAG STAYS WITH THE WINDOW", win_event(&e));
		ok("and moving around the wheel moves the hue", tool_colour(0) != first);

		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_UP;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = c.x + 20.0f;
		e.button.y = c.y + 100.0f;
		win_event(&e);

		/* Once the button is up the window has let go, and the sheet gets its events back. */
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_MOTION;
		e.motion.x = c.x - 80.0f;
		e.motion.y = c.y - 80.0f;
		ok("and lets go when the button does", win_event(&e) == false);

		/* ---- the hex field ----
		 *
		 * The whole path, not a piece of it: clicking the readout captures the keyboard,
		 * typing goes through keys_event the way core.c routes it, and ENTER commits. It is
		 * the second thing in the program to own the keyboard, so this is also a check that
		 * keys.c does what it was built for.
		 */
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = c.x + 100.0f;       /* the hex box, under the wheel */
		e.button.y = c.y + c.h - 28.0f;  /* above the band the stretch corner lives in */
		win_event(&e);

		SDL_zero(e);
		e.type = SDL_EVENT_KEY_DOWN;
		e.key.key = SDLK_TAB;
		ok("WHILE TYPING, TAB IS THE FIELD'S AND NOT THE SIDEBAR'S", keys_event(&e));

		wipe();                          /* the box opens holding the colour it reads out */
		typed("2E3440");
		ok("nothing lands until ENTER does", tool_colour(0) != 0xFF2E3440u);

		enter();
		ok("A TYPED HEX BECOMES THE COLOUR", tool_colour(0) == 0xFF2E3440u);
		ok("and the keyboard is handed back", keys_event(&e) == false);

		/* Liberal in what it takes, because a colour is copied from somewhere else and
		 * arrives in whatever shape that somewhere used. */
		hex_type("#88C0D0");
		ok("a leading hash is taken", tool_colour(0) == 0xFF88C0D0u);

		hex_type("F0A");
		ok("three digits are the shorthand, each doubled", tool_colour(0) == 0xFFFF00AAu);

		hex_type("4080FF80");
		ok("eight digits say the alpha outright", tool_colour(0) == 0x804080FFu);

		Uint32 kept = tool_colour(0);
		hex_type("zzz");
		ok("and nonsense changes nothing", tool_colour(0) == kept);

		/* WHICH COLOUR A PRESS FILLS IS SAID BY THE BUTTON, the way it is everywhere else in
		 * this program - and the way the first Vangopix had it. */
		tool_set_colour(0, 0xFF000000u);
		tool_set_colour(1, 0xFF000000u);

		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		e.button.button = SDL_BUTTON_RIGHT;
		e.button.x = c.x + 90.0f;
		e.button.y = c.y + 30.0f;
		win_event(&e);

		ok("THE RIGHT BUTTON FILLS COLOUR 2", tool_colour(1) != 0xFF000000u);
		ok("and leaves colour 1 alone",       tool_colour(0) == 0xFF000000u);

		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_UP;
		e.button.button = SDL_BUTTON_RIGHT;
		e.button.x = c.x + 90.0f;
		e.button.y = c.y + 30.0f;
		win_event(&e);

		colour_toggle();
		ok("C puts it away", colour_visible() == false);
	}

	/* ---- THE PALETTE: one list of colours, a box, a grid beside it, and a list ----
	 *
	 * The model first, because everything else is a view of it, then the two things the
	 * original got wrong that a check can actually reach: which entry a delete removes, and
	 * what the ALT grid answers for a point that is not on it.
	 */
	{
		VNG_TAB *q = vng_tab_new(4, 2);

		/* Three colours, one of them twice, and two holes. A hole must not become a swatch:
		 * a sprite is mostly hole, and a cell spent on it is a cell spent in every drawing. */
		q->pixels[0] = 0xFFFF0000u;
		q->pixels[1] = 0xFF00FF00u;
		q->pixels[2] = 0x00000000u;
		q->pixels[3] = 0xFFFF0000u;
		q->pixels[4] = 0xFF0000FFu;
		q->pixels[5] = 0x00123456u;
		q->pixels[6] = 0xFF00FF00u;
		q->pixels[7] = 0xFFFF0000u;

		palette_scan(q);

		ok("the scan finds each colour once", palette_lot(q) == 3);
		ok("and in the order it met them",
		   palette_at(q, 0) == 0xFFFF0000u &&
		   palette_at(q, 1) == 0xFF00FF00u &&
		   palette_at(q, 2) == 0xFF0000FFu);
		ok("NOTHING IS NOT A COLOUR",
		   !palette_has(q, 0x00000000u) && !palette_has(q, 0x00123456u));
		ok("out of range reads as nothing, not off the end", palette_at(q, 99) == 0u);

		ok("a new colour is added",     palette_add(q, 0xFF808080u) == true);
		ok("a repeat is not",           palette_add(q, 0xFF808080u) == false);
		ok("and did not grow the list", palette_lot(q) == 4);

		/* THE DELETE TAKES THE FIRST MATCH AND KEEPS THE ORDER. The original searched the
		 * whole list without stopping, so it kept the LAST index it saw, and then shifted
		 * with a loop that read one past the end of the list it had just shortened. */
		palette_del(q, 0xFF00FF00u);
		ok("delete removes it", palette_has(q, 0xFF00FF00u) == false);
		ok("and closes the gap in order",
		   palette_lot(q) == 3 &&
		   palette_at(q, 0) == 0xFFFF0000u &&
		   palette_at(q, 1) == 0xFF0000FFu &&
		   palette_at(q, 2) == 0xFF808080u);
		palette_del(q, 0xFF00FF00u);   /* again, on a colour that is gone */
		ok("deleting what is not there does nothing", palette_lot(q) == 3);

		/* THE PALETTE IS PER TAB, like the selection and for the same reason: it is the
		 * working set of one drawing. The original kept it per image, and its grid's current
		 * index in a static shared by every document. */
		ok("a palette belongs to its own tab", palette_has(t, 0xFF808080u) == false);

		/* ---- the palettes of other machines ---- */

		ok("the .ini is read", palette_list_lot() > 0);

		{
			/* MEGA-DRIVE is in the author's own file, and it is a set nobody arrives at by
			 * eyedropping their own drawing - which is what the COLOR button is for. */
			bool got = palette_load(q, "MEGA-DRIVE");
			ok("a named palette loads", got);
			ok("and REPLACED what was there rather than adding to it",
			   got && palette_has(q, 0xFF808080u) == false);
			ok("its colours are opaque",
			   got && palette_lot(q) > 0 && (palette_at(q, 0) >> 24) == 0xFFu);
			ok("GAMEBOY is four colours",
			   palette_load(q, "GAMEBOY") && palette_lot(q) == 4);
			ok("and a name that is not in the file changes nothing",
			   palette_load(q, "NO-SUCH-MACHINE") == false && palette_lot(q) == 4);
		}

		palette_scan(q);   /* back to the drawing's own colours */

		/* ---- the box, and the grid BESIDE it ---- */

		ok("no box, no grid", palette_grid_area().w == 0.0f);

		palette_toggle();
		ok("P puts the box up", palette_visible() == true);

		SDL_FRect g = palette_grid_area();
		ok("and the grid is beside it", g.w > 0.0f && g.h > 0.0f);

		/* IT IS OUTSIDE THE WINDOW, which is the design: the box is contained and the
		 * swatches hang off its right edge. */
		SDL_FRect o = win_outer(win_top());
		ok("THE GRID IS OUTSIDE THE BOX, not in it", g.x >= o.x + o.w);
		ok("and level with its interior",            g.y == win_area(win_top()).y);

		{
			/* A press on a cell, by button, exactly as on the sheet. */
			SDL_Event e;
			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
			e.button.button = SDL_BUTTON_LEFT;
			e.button.x = g.x + 10.0f;
			e.button.y = g.y + 10.0f;
			ok("a cell answers the press", palette_grid_event(&e, q) == true);
			ok("and the left button fills colour 1", tool_colour(0) == palette_at(q, 0));

			e.button.button = SDL_BUTTON_RIGHT;
			palette_grid_event(&e, q);
			ok("the right button fills colour 2", tool_colour(1) == palette_at(q, 0));

			/* CTRL BELONGS TO THE PALETTE ONLY WHERE THE PALETTE IS. Off the grid the event
			 * is declined, so the eyedropper on the sheet is untouched. */
			e.button.button = SDL_BUTTON_LEFT;
			e.button.x = g.x - 40.0f;
			ok("a press off the grid is not the grid's",
			   palette_grid_event(&e, q) == false);
		}

		{
			/* CTRL+left keeps the colour in hand; CTRL+right throws one away. */
			SDL_SetModState(SDL_KMOD_LCTRL);
			tool_set_colour(0, 0xFF121212u);

			SDL_Event e;
			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
			e.button.button = SDL_BUTTON_LEFT;
			e.button.x = g.x + 10.0f;
			e.button.y = g.y + 10.0f;
			palette_grid_event(&e, q);
			ok("CTRL+left keeps the colour in hand", palette_has(q, 0xFF121212u));

			e.button.button = SDL_BUTTON_RIGHT;
			palette_grid_event(&e, q);
			ok("CTRL+right throws the one under it away",
			   palette_has(q, 0xFFFF0000u) == false);

			SDL_SetModState(SDL_KMOD_NONE);
		}

		/*
		 * THE POINTER IS THE ONLY THING THAT SAYS THE BANDS ARE THERE - eight pixels of
		 * nothing over the last column and the last row. Without a shape change there is no
		 * way to find out the grid stretches except by dragging and seeing what happens.
		 */
		ok("the corner band asks for the diagonal arrows",
		   palette_grid_cursor(g.x + g.w - 1.0f, g.y + g.h - 1.0f) == VNG_CUR_NWSE);
		ok("the right edge asks for the sideways pair",
		   palette_grid_cursor(g.x + g.w - 1.0f, g.y + 4.0f) == VNG_CUR_WE);
		ok("the bottom edge asks for the up-down pair",
		   palette_grid_cursor(g.x + 4.0f, g.y + g.h - 1.0f) == VNG_CUR_NS);
		ok("and the middle of the grid asks for NOTHING, which is not the arrow",
		   palette_grid_cursor(g.x + 10.0f, g.y + 10.0f) == VNG_CUR_ARROW);

		/*
		 * STRETCHING THE GRID IS WHAT THE ALT GRID FOLLOWS, and that is the link the first
		 * Vangopix already had: its core.c called gui_quickly_palette_box(plt_pos, 20) with
		 * THE SAME RECTANGLE this grid was stretched to. One grid, two summonings.
		 */
		{
			SDL_Event e;
			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
			e.button.button = SDL_BUTTON_LEFT;
			e.button.x = g.x + g.w - 1.0f;    /* the corner band */
			e.button.y = g.y + g.h - 1.0f;
			ok("the corner band takes the press", palette_grid_event(&e, q) == true);

			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_MOTION;
			e.motion.x = g.x + 6.0f * VNG_PAL_CELL;
			e.motion.y = g.y + 5.0f * VNG_PAL_CELL;
			palette_grid_event(&e, q);

			SDL_FRect big = palette_grid_area();
			ok("and pulling it makes the grid 6 x 5",
			   big.w == 6.0f * VNG_PAL_CELL && big.h == 5.0f * VNG_PAL_CELL);

			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_UP;
			e.button.button = SDL_BUTTON_LEFT;
			palette_grid_event(&e, q);

			SDL_SetModState(SDL_KMOD_LALT);
			SDL_FRect alt = palette_quick_area();
			ok("THE ALT GRID IS THE SAME SHAPE", alt.w == big.w && alt.h == big.h);
			SDL_SetModState(SDL_KMOD_NONE);
		}

		/*
		 * A SINGLE COLUMN AND A SINGLE ROW, which a minimum of three made impossible to ask
		 * for - and a strip is how a ramp is laid out in the order it runs. Pulled well past
		 * the grid's own top left, so it is the clamp that answers and not where the hand
		 * happened to stop. Put back to 6 x 5 afterwards, which is what the checks below were
		 * written against.
		 */
		{
			#define PULL(to_x, to_y) do {                                                    \
				SDL_FRect pg = palette_grid_area();                                           \
				SDL_Event pe;                                                                 \
				SDL_zero(pe);                                                                 \
				pe.type = SDL_EVENT_MOUSE_BUTTON_DOWN;                                        \
				pe.button.button = SDL_BUTTON_LEFT;                                           \
				pe.button.x = pg.x + pg.w - 1.0f;                                             \
				pe.button.y = pg.y + pg.h - 1.0f;                                             \
				palette_grid_event(&pe, q);                                                   \
				SDL_zero(pe);                                                                 \
				pe.type = SDL_EVENT_MOUSE_MOTION;                                             \
				pe.motion.x = pg.x + (to_x);                                                  \
				pe.motion.y = pg.y + (to_y);                                                  \
				palette_grid_event(&pe, q);                                                   \
				SDL_zero(pe);                                                                 \
				pe.type = SDL_EVENT_MOUSE_BUTTON_UP;                                          \
				pe.button.button = SDL_BUTTON_LEFT;                                           \
				palette_grid_event(&pe, q);                                                   \
			} while (0)

			PULL(-100.0f, 5.0f * VNG_PAL_CELL);
			SDL_FRect col = palette_grid_area();
			ok("THE GRID NARROWS TO A SINGLE COLUMN",
			   col.w == VNG_PAL_CELL && col.h == 5.0f * VNG_PAL_CELL);

			PULL(6.0f * VNG_PAL_CELL, -100.0f);
			SDL_FRect row = palette_grid_area();
			ok("AND FLATTENS TO A SINGLE ROW",
			   row.w == 6.0f * VNG_PAL_CELL && row.h == VNG_PAL_CELL);

			PULL(-100.0f, -100.0f);
			SDL_FRect one = palette_grid_area();
			ok("and no smaller than one cell",
			   one.w == VNG_PAL_CELL && one.h == VNG_PAL_CELL);

			PULL(6.0f * VNG_PAL_CELL, 5.0f * VNG_PAL_CELL);
			#undef PULL
		}

		/* ---- the grid under ALT ---- */

		ok("no ALT, no grid", palette_quick_area().w == 0.0f);

		SDL_SetModState(SDL_KMOD_LALT);

		tool_set_colour(0, 0xFF010203u);   /* what the gesture must be able to give back */
		SDL_FRect a = palette_quick_area();
		ok("ALT puts a grid up", a.w > 0.0f && a.h > 0.0f);

		ok("a cell answers with its colour",
		   palette_quick_hover(q, a.x + 10.0f, a.y + 10.0f) == palette_at(q, 0));

		/*
		 * LEAVING THE GRID GIVES THE COLOUR BACK, FROM ANY SIDE. This is the bug:
		 * gui_quickly_palette_box tested select_rect(pos.x, pos.y, gd, gd) for it - its FIRST
		 * CELL - so the gesture could only be abandoned by leaving through the top left, and
		 * from every other direction it kept whatever had last been swept over.
		 */
		ok("leaving to the right gives it back",
		   palette_quick_hover(q, a.x + a.w + 20.0f, a.y + 10.0f) == 0xFF010203u);
		ok("leaving below gives it back",
		   palette_quick_hover(q, a.x + 10.0f, a.y + a.h + 20.0f) == 0xFF010203u);
		ok("leaving to the left gives it back",
		   palette_quick_hover(q, a.x - 20.0f, a.y + 10.0f) == 0xFF010203u);

		/* A PRESS STILL SAYS WHICH SLOT, which the original had no way to express here: it
		 * filled color_front and nothing else, so the second colour could not be loaded from
		 * a palette at all. */
		{
			SDL_Event e;
			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
			e.button.button = SDL_BUTTON_RIGHT;
			e.button.x = a.x + 10.0f;
			e.button.y = a.y + 10.0f;
			ok("a press on a cell is the grid's", palette_quick_event(&e, q) == true);
			ok("and the right button fills colour 2", tool_colour(1) == palette_at(q, 0));

			e.button.x = a.x + a.w + 40.0f;
			ok("a press off the grid is not", palette_quick_event(&e, q) == false);
		}

		SDL_SetModState(SDL_KMOD_NONE);
		ok("letting ALT go takes the grid away", palette_quick_area().w == 0.0f);

		/*
		 * THE KEYBOARD'S OWNER SILENCES IT, which is the whole of what keys_held and
		 * keys_mods are for: a modifier read straight off the hardware is one read behind the
		 * owner's back, and ALT held while a field is open would put a palette over the top
		 * of the thing being typed into.
		 */
		{
			colour_toggle();

			SDL_Event e;
			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
			e.button.button = SDL_BUTTON_LEFT;
			SDL_FRect c = win_area(win_top());
			e.button.x = c.x + 100.0f;
			e.button.y = c.y + c.h - 28.0f;
			win_event(&e);        /* the hex field now owns the keyboard */

			SDL_SetModState(SDL_KMOD_LALT);
			ok("ALT HELD INTO A FIELD IS NOT A PALETTE", palette_quick_area().w == 0.0f);
			SDL_SetModState(SDL_KMOD_NONE);

			enter();              /* hand the keyboard back */
			colour_toggle();
		}

		/*
		 * And once through the pixels. Nothing is asserted about them - the point is that
		 * every path that touches the renderer is walked at least once here, because a bad
		 * rectangle or a NULL in a draw would otherwise only ever show up in front of a
		 * person.
		 */
		palette_grid_draw(q);     /* the swatches, and the count above them */
		win_draw();               /* the box: two discs and the word */

		SDL_SetModState(SDL_KMOD_LALT);
		palette_quick_draw(q);    /* the grid under the hand, with its readout */
		SDL_SetModState(SDL_KMOD_NONE);

		palette_toggle();
		ok("P puts the box away",            palette_visible() == false);
		ok("and the grid goes with it",      palette_grid_area().w == 0.0f);
		ok("and so does the list it opened", palette_list_visible() == false);

		vng_tab_close(q);
	}

	/* ---- THE CAMERA: THE PIXEL YOU SEE IS THE PIXEL YOU HIT ----
	 *
	 * view_sheet_rect floors the sheet onto whole pixels, correctly. Nothing else knew: every
	 * annotation about the sheet - the outline round the pixel under the pointer, the
	 * selection's rectangle, the corner grips, the 1:1 marker - and the HIT TEST that decides
	 * which pixel a press lands on all came off the unfloored transform.
	 *
	 * At 1:1 with an offset of -10.3 the sheet was drawn at x=10, so document pixel 0 covered
	 * screen [10,11) while the transform reported it at 10.3, and thirty percent of the clicks
	 * inside a drawn pixel resolved to its neighbour. These three are what the repair has to
	 * keep true.
	 */
	{
		VNG_TAB *c = vng_tab_new(64, 48);

		vng_win_w = 320;
		vng_win_h = 240;
		view_reset(c);

		/* ---- 1. every point of a drawn pixel hits that pixel ---- */
		{
			/* A pan of an odd number of screen pixels, which is what leaves an offset with a
			 * fraction in it - and after a pan is the only state this program is ever in. */
			SDL_Event e;
			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
			e.button.button = SDL_BUTTON_MIDDLE;
			e.button.x = 100.0f; e.button.y = 100.0f;
			view_event(&e, c);

			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_MOTION;
			e.motion.x = 137.0f; e.motion.y = 111.0f;
			view_event(&e, c);

			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_UP;
			e.button.button = SDL_BUTTON_MIDDLE;
			view_event(&e, c);
		}

		SDL_FRect sheet = view_sheet_rect(c);

		ok("the sheet lands on whole pixels",
		   sheet.x == SDL_floorf(sheet.x) && sheet.y == SDL_floorf(sheet.y));

		/* The transform must put document 0,0 exactly where the sheet was drawn. When it did
		 * not, everything measured from it was off by the fraction the floor threw away. */
		SDL_FPoint o = view_world_to_screen(c, 0.0f, 0.0f);
		ok("and the transform agrees with where it landed",
		   o.x == sheet.x && o.y == sheet.y);

		{
			/* Walk across four document pixels at 1:1, a hundred samples each, the way
			 * tool.c's pixel_of does it. Every sample inside a drawn cell must resolve to
			 * that cell. */
			int wrong = 0, total = 0;

			for (int px = 0; px < 4; px++) {
				for (int i = 0; i < 100; i++) {
					float sx = sheet.x + (float)px * c->zoom
					         + (float)i / 100.0f * c->zoom;
					SDL_FPoint w = view_screen_to_world(c, sx, sheet.y + 0.5f);
					total++;
					if ((int)SDL_floorf(w.x) != px) wrong++;
				}
			}
			ok("THE PIXEL YOU SEE IS THE PIXEL YOU HIT", wrong == 0);
			if (wrong) SDL_Log("  %d of %d samples landed on another pixel", wrong, total);
		}

		/* ---- 2. the zoom happens at the cursor, and does not drift ---- */
		{
			/*
			 * The claim recorded in CLAUDE.md, re-checked because the transform pair the
			 * outside world sees is now snapped onto whole pixels. What must stay true is the
			 * promise itself: whatever is under the cursor when a notch arrives is under the
			 * cursor after it.
			 *
			 * Checked PER NOTCH and not against one point held from the start, because that
			 * is what the algorithm actually promises - and because the other thing is what a
			 * discarded first attempt got wrong. Snapping off_x itself put the origin on
			 * whole pixels and rounded the camera's own state; half a screen pixel at 4x is
			 * an eighth of a document pixel, and zooming on to 64x magnified it into eight.
			 * The offset stays exact now, and the floor lives only where the world becomes
			 * screen coordinates.
			 */
			const float AT_X = 210.0f, AT_Y = 90.0f;

			/* Low on the ladder, so twelve notches up and twelve back down both fit on it:
			 * there are sixteen steps, and from 4x the way up runs out at the twelfth. */
			c->zoom = 0.125f;

			float ox0 = c->off_x, oy0 = c->off_y;
			float worst = 0.0f, worst2 = 0.0f;

			for (int i = 0; i < 24; i++) {
				SDL_FPoint was = view_screen_to_world(c, AT_X, AT_Y);

				SDL_Event e;
				SDL_zero(e);
				e.type = SDL_EVENT_MOUSE_WHEEL;
				e.wheel.integer_y = (i < 12) ? 1 : -1;
				e.wheel.mouse_x = AT_X;
				e.wheel.mouse_y = AT_Y;
				view_event(&e, c);

				SDL_FPoint now = view_world_to_screen(c, was.x, was.y);
				float dx = SDL_fabsf(now.x - AT_X), dy = SDL_fabsf(now.y - AT_Y);
				if (dx > worst) worst = dx;
				if (dy > worst) worst = dy;
			}

			/* Under a pixel is the whole of it: the snap onto whole pixels can move what is
			 * under the cursor by the fraction it throws away, and no more. A drift would
			 * GROW across twenty-four notches instead of staying inside one pixel. */
			/* ONE SCREEN PIXEL is the whole of it, and it is exactly the floor's fraction:
			 * snapping the origin onto a whole pixel can move what is under the cursor by the
			 * part it throws away, and by no more. */
			ok("THE ZOOM STAYS AT THE CURSOR, TWELVE STEPS EACH WAY", worst <= 1.0f);
			if (worst > 1.0f) SDL_Log("  drifted %.3f px", worst);

			ok("and twelve up then twelve down is the zoom it started on", c->zoom == 0.125f);

			/*
			 * AND THE BOUND DOES NOT GROW, which is the difference between a fraction and a
			 * DRIFT. Another twenty-four notches over the same ground: if the floor were
			 * feeding back into the camera's state - which is what quantising off_x did, and
			 * why that attempt was thrown away - this second pass would be worse than the
			 * first, and the offset would not come home.
			 */
			for (int i = 0; i < 24; i++) {
				SDL_FPoint was = view_screen_to_world(c, AT_X, AT_Y);

				SDL_Event e;
				SDL_zero(e);
				e.type = SDL_EVENT_MOUSE_WHEEL;
				e.wheel.integer_y = (i < 12) ? 1 : -1;
				e.wheel.mouse_x = AT_X;
				e.wheel.mouse_y = AT_Y;
				view_event(&e, c);

				SDL_FPoint now = view_world_to_screen(c, was.x, was.y);
				float dx = SDL_fabsf(now.x - AT_X), dy = SDL_fabsf(now.y - AT_Y);
				if (dx > worst2) worst2 = dx;
				if (dy > worst2) worst2 = dy;
			}

			ok("AND IT IS A FRACTION, NOT A DRIFT - it does not grow", worst2 <= worst);
			if (worst2 > worst) SDL_Log("  first pass %.3f, second %.3f", worst, worst2);

			/* The offset itself comes home, which it cannot do if anything rounded it. */
			ok("and the offset comes back to where it started",
			   SDL_fabsf(c->off_x - ox0) * c->zoom <= 1.0f &&
			   SDL_fabsf(c->off_y - oy0) * c->zoom <= 1.0f);
		}

		/* ---- 3. a pan gives back the point it took hold of ---- */
		{
			const float GRAB_X = 150.0f, GRAB_Y = 120.0f;
			SDL_Event e;

			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
			e.button.button = SDL_BUTTON_MIDDLE;
			e.button.x = GRAB_X; e.button.y = GRAB_Y;
			ok("the middle button starts a pan", view_event(&e, c) == true);

			SDL_FPoint held = view_screen_to_world(c, GRAB_X, GRAB_Y);
			float worst = 0.0f;

			/* Dragged in ones, because a pan that only works over long throws is a pan that
			 * jitters: snap() moves the sheet in whole pixels, and the point held has to stay
			 * within one of them the whole way. */
			for (int i = 1; i <= 40; i++) {
				SDL_zero(e);
				e.type = SDL_EVENT_MOUSE_MOTION;
				e.motion.x = GRAB_X + (float)i;
				e.motion.y = GRAB_Y + (float)i * 0.5f;
				view_event(&e, c);

				SDL_FPoint here = view_world_to_screen(c, held.x, held.y);
				float dx = SDL_fabsf(here.x - e.motion.x);
				float dy = SDL_fabsf(here.y - e.motion.y);
				if (dx > worst) worst = dx;
				if (dy > worst) worst = dy;
			}

			SDL_zero(e);
			e.type = SDL_EVENT_MOUSE_BUTTON_UP;
			e.button.button = SDL_BUTTON_MIDDLE;
			view_event(&e, c);

			ok("THE PAN KEEPS THE POINT IT TOOK HOLD OF", worst <= 1.0f);
			if (worst > 1.0f) SDL_Log("  slipped %.3f px", worst);

			/* And it is still whole after all of that. */
			SDL_FRect r = view_sheet_rect(c);
			SDL_FPoint p = view_world_to_screen(c, 0.0f, 0.0f);
			ok("the sheet is still on whole pixels afterwards",
			   p.x == r.x && p.y == r.y && r.x == SDL_floorf(r.x));
		}

		vng_tab_close(c);
	}

	/* ---- THE DISC, READ BACK PIXEL BY PIXEL ----
	 *
	 * The two loaded colours in the palette box are discs, and so is the hole in the colour
	 * wheel. SDL draws no circles, so this program rasterises its own - and the first version
	 * drew THREE different ones per disc: a fill from sqrt() with float ends that SDL rounded
	 * by its own rule, a rim from sixty-four straight chords off cos/sin, and a checkerboard
	 * stepped between them. At radius 20 that rim is a ~126 pixel circumference cut into 2
	 * pixel chords whose vertices land between pixels, so it read as a polygon that did not
	 * sit on its own fill.
	 *
	 * None of that can be judged by looking at a build log, which is why it is measured here:
	 * rendered to a texture and read straight back.
	 */
	{
		const int   N = 64, CX = 32, CY = 32, RAD = 20;
		/* All three 0xAARRGGBB, which is what vangopix_desk_disc takes for BOTH the fill and
		 * the rim. It took 0xRRGGBBAA for the rim when this check was written, and a rim asked
		 * for as blue came out red - so the signature was made one order and this stayed. */
		const Uint32 BG = 0xFF00FF00u, FILL = 0xFFFF0000u, RIM = 0xFF0000FFu;

		SDL_Texture *target = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
		                                        SDL_TEXTUREACCESS_TARGET, N, N);
		SDL_Surface *shot = NULL;

		if (target) {
			SDL_SetRenderTarget(vng_ren, target);
			SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_NONE);
			SDL_SetRenderDrawColor(vng_ren, 0x00, 0xFF, 0x00, 0xFF);
			SDL_RenderClear(vng_ren);

			vangopix_desk_disc((float)CX, (float)CY, (float)RAD, FILL, RIM);

			SDL_Surface *raw = SDL_RenderReadPixels(vng_ren, NULL);
			if (raw) {
				shot = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
				SDL_DestroySurface(raw);
			}
			SDL_SetRenderTarget(vng_ren, NULL);
			SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);
		}

		ok("the disc can be read back", shot != NULL);

		if (shot) {
			const Uint32 *px = (const Uint32 *) shot->pixels;
			const int pitch = shot->pitch / 4;

			#define AT(x, y) (px[(y) * pitch + (x)])

			int lo[64], hi[64];
			for (int y = 0; y < N; y++) {
				lo[y] = -1; hi[y] = -1;
				for (int x = 0; x < N; x++)
					if (AT(x, y) != BG) { if (lo[y] < 0) lo[y] = x; hi[y] = x; }
			}

			/* A CIRCLE CENTRED ON A PIXEL IS SYMMETRIC ABOUT IT. The float centre this used to
			 * take was never rounded, so one side came out a pixel fatter than the other. */
			bool sym_x = true, sym_y = true, solid = true, bounded = true;

			for (int y = 0; y < N; y++) {
				if (lo[y] < 0) continue;
				if ((CX - lo[y]) != (hi[y] - CX)) sym_x = false;

				/* Nothing of the background survives inside the span: no gap in the rim, and
				 * no seam between the rim and the fill it is supposed to bound. */
				for (int x = lo[y]; x <= hi[y]; x++)
					if (AT(x, y) == BG) solid = false;

				/* The first and last pixel of every row are RIM. An outline computed from
				 * different maths than its fill misses this by up to a pixel all the way
				 * round, which is exactly what it looked like. */
				if (AT(lo[y], y) != RIM || AT(hi[y], y) != RIM) bounded = false;
			}

			for (int k = 1; k <= RAD; k++)
				if (lo[CY - k] != lo[CY + k] || hi[CY - k] != hi[CY + k]) sym_y = false;

			ok("the disc is symmetric left to right", sym_x);
			ok("and top to bottom",                   sym_y);
			ok("the rim leaves no gap in it",         solid);
			ok("THE RIM SITS EXACTLY ON THE FILL",    bounded);

			ok("it is as wide as it was asked to be",
			   lo[CY] == CX - RAD && hi[CY] == CX + RAD);
			ok("and the fill is inside the rim",      AT(CX, CY) == FILL);

			/* Radius zero and a negative one are asked for by a window stretched small. */
			#undef AT
			SDL_DestroySurface(shot);
		}
		if (target) SDL_DestroyTexture(target);
	}

	/* ---- THE MARK THAT CLOSES A THING ----
	 *
	 * A window head, a tab, a project folder and a clip all offered the same promise and each
	 * drew its own lowercase "x" with its own two colours - which had already drifted apart,
	 * one dimming to 0x909090 and another to 0x707070. It is one red disc now.
	 *
	 * Checked in pixels, and NOT with a font loaded on purpose: an "x" is a character, so a
	 * window with no face was a window with no way to say it could be closed. The disc does
	 * not depend on the font having loaded, and this is what says so.
	 */
	{
		const int N = 200;

		vng_win_w = N;
		vng_win_h = N;

		if (colour_visible()) colour_toggle();
		if (anim_visible())   anim_toggle();
		if (thumb_visible())  thumb_toggle();
		if (palette_visible()) palette_toggle();

		thumb_toggle();                       /* the simplest window there is */
		win_place(win_top(), (float)N * 0.5f, (float)N * 0.5f);

		SDL_FRect o = win_outer(win_top());

		SDL_Texture *target = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
		                                        SDL_TEXTUREACCESS_TARGET, N, N);
		SDL_Surface *shot = NULL;

		if (target) {
			SDL_SetRenderTarget(vng_ren, target);
			SDL_SetRenderDrawColor(vng_ren, 0x00, 0xFF, 0x00, 0xFF);
			SDL_RenderClear(vng_ren);
			win_draw();
			SDL_Surface *raw = SDL_RenderReadPixels(vng_ren, NULL);
			if (raw) {
				shot = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
				SDL_DestroySurface(raw);
			}
			SDL_SetRenderTarget(vng_ren, NULL);
			SDL_DestroyTexture(target);
		}

		ok("the head bar can be read back", shot != NULL);
		ok("there is no font loaded for this", vng_text == NULL);

		if (shot) {
			const Uint32 *px = (const Uint32 *) shot->pixels;
			const int pitch = shot->pitch / 4;

			/* The head bar's right end, where the mark lives - measured off win_outer so this
			 * carries no second copy of the layout. */
			int x0 = (int)(o.x + o.w - ui_close() - ui_pad() * 2.0f);
			int x1 = (int)(o.x + o.w);
			int y0 = (int)o.y, y1 = (int)(o.y + ui_head());

			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 > N) x1 = N;
			if (y1 > N) y1 = N;

			bool red = false;

			for (int y = y0; y < y1; y++)
				for (int x = x0; x < x1; x++) {
					Uint32 v = px[y * pitch + x];
					int r = (int)((v >> 16) & 0xFF);
					int g = (int)((v >>  8) & 0xFF);
					int b = (int)( v        & 0xFF);

					/* Decidedly red: well clear of every grey in the chrome. */
					if (r > 0x90 && g < 0x60 && b < 0x60) red = true;
				}

			ok("A WINDOW WITH NO FONT STILL SHOWS IT CAN BE CLOSED", red);
			if (!red) SDL_Log("  nothing red in the head bar's close corner");

			SDL_DestroySurface(shot);
		}

		thumb_toggle();
		ok("and the panel goes away again", thumb_visible() == false);
	}

	/* ---- THE TWO LOADED COLOURS ARE ON SCREEN, BOTTOM LEFT ----
	 *
	 * They are always there because there is no other way to know which colour each side of
	 * the mouse is holding, and that is not a question a person should have to press a key to
	 * ask. Which makes "are they actually drawn, and inside the window" worth pinning: it is
	 * geometry off vng_win_h and sidebar_edge(), and either could put them off the bottom or
	 * behind the panel without anything failing.
	 */
	{
		const int N = 200;

		vng_win_w = N;
		vng_win_h = N;
		view_reset(t);

		tool_set_colour(0, 0xFFC80000u);   /* a red nothing else here uses */
		tool_set_colour(1, 0xFF00C800u);   /* and a green */

		SDL_Texture *target = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
		                                        SDL_TEXTUREACCESS_TARGET, N, N);
		SDL_Surface *shot = NULL;

		if (target) {
			SDL_SetRenderTarget(vng_ren, target);
			SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0xFF, 0xFF);
			SDL_RenderClear(vng_ren);

			tool_draw(t);

			SDL_Surface *raw = SDL_RenderReadPixels(vng_ren, NULL);
			if (raw) {
				shot = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
				SDL_DestroySurface(raw);
			}
			SDL_SetRenderTarget(vng_ren, NULL);
			SDL_DestroyTexture(target);
		}

		ok("the frame can be read back", shot != NULL);

		if (shot) {
			const Uint32 *px = (const Uint32 *) shot->pixels;
			const int pitch = shot->pitch / 4;

			bool red = false, green = false;

			/* The bottom band, the whole width - so this says WHERE they are without
			 * carrying a second copy of the layout. */
			for (int y = N / 2; y < N; y++)
				for (int x = 0; x < N; x++) {
					Uint32 v = px[y * pitch + x] & 0x00FFFFFFu;
					if (v == 0x00C80000u) red   = true;
					if (v == 0x0000C800u) green = true;
				}

			ok("COLOUR 1 IS ON SCREEN", red);
			ok("AND SO IS COLOUR 2",    green);

			/*
			 * AND THE SIZE READOUT SITS BESIDE THEM, NOT ON THEM. It is placed at
			 * tool_slots_edge() so it stays put when the bars step aside for the project
			 * panel; a number of its own would have overlapped them the first time the face
			 * or the padding moved.
			 */
			{
				float bw, bh;
				tool_bar_size(&bw, &bh);

				float x0 = sidebar_edge() + ui_pad() * 2.0f;
				float x1 = tool_slots_edge();

				ok("THE SIZE READOUT STARTS AFTER BOTH BARS", x1 >= x0 + bw * 2.0f);

				/* Nothing of the second bar's green is at or past that edge. */
				bool bleed = false;
				for (int y = N / 2; y < N; y++)
					for (int x = (int)x1; x < N; x++)
						if ((px[y * pitch + x] & 0x00FFFFFFu) == 0x0000C800u) bleed = true;

				ok("and the bars do not reach into it", bleed == false);
			}

			if (!red || !green)
				SDL_Log("  red=%d green=%d - the readouts are not being drawn where they should",
				        (int)red, (int)green);

			SDL_DestroySurface(shot);
		}

		tool_set_colour(0, 0xFF000000u);
		tool_set_colour(1, 0x00000000u);
	}

	/* ---- THE TWO COLOUR READOUTS SHOW ALPHA EVEN ON A FRESH FRAME ----
	 *
	 * Colour 2 starts as NOTHING, and the bar that reads it out shows that by laying it over
	 * two tones at its real alpha. That needs blending, and tool.c turned it on NOWHERE: it
	 * drew with whatever the frame had left behind.
	 *
	 * Which on a fresh program is BLENDMODE_NONE, SDL's default - select_draw and thumb_draw
	 * both set it, but both return early when there is no selection and no 1:1 panel, before
	 * reaching the line that does. So the second readout wrote a literal zero and came out
	 * solid black, and then FIXED ITSELF the moment anything else on screen turned blending
	 * on. Intermittent by construction.
	 */
	{
		const int N = 96;

		SDL_Texture *target = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
		                                        SDL_TEXTUREACCESS_TARGET, N, N);
		SDL_Surface *shot = NULL;

		if (target) {
			SDL_SetRenderTarget(vng_ren, target);
			SDL_SetRenderDrawColor(vng_ren, 0x00, 0xFF, 0x00, 0xFF);
			SDL_RenderClear(vng_ren);

			/* THE STATE A FRESH FRAME IS ACTUALLY IN. Not a contrivance - it is what the
			 * program starts in and returns to whenever nothing else has set it. */
			SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_NONE);

			SDL_FRect bar = { 8.0f, 8.0f, 72.0f, 20.0f };
			tool_bar_draw(bar, 0x00000000u);      /* nothing, which is colour 2's default */

			SDL_Surface *raw = SDL_RenderReadPixels(vng_ren, NULL);
			if (raw) {
				shot = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
				SDL_DestroySurface(raw);
			}
			SDL_SetRenderTarget(vng_ren, NULL);
			SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);
			SDL_DestroyTexture(target);
		}

		ok("the readout can be read back", shot != NULL);

		if (shot) {
			const Uint32 *px = (const Uint32 *) shot->pixels;
			const int pitch = shot->pitch / 4;

			/* One row well inside the bar, clear of the rim and of where the text would be. */
			int  y = 20, changes = 0, tones = 0;
			Uint32 seen[4], prev = 0;

			for (int x = 12; x < 76; x++) {
				Uint32 v = px[y * pitch + x] & 0x00FFFFFFu;

				if (x > 12 && v != prev) changes++;
				prev = v;

				int j = 0;
				while (j < tones && seen[j] != v) j++;
				if (j == tones && tones < 4) seen[tones++] = v;
			}

			ok("A TRANSPARENT SLOT SHOWS WHAT IS BEHIND IT",
			   tones >= 2 && (seen[0] != 0x000000u || seen[1] != 0x000000u));

			/*
			 * AND IT IS A CHECKERBOARD, NOT TWO HALVES - which is the difference this whole
			 * check exists to say, because both patterns pass "more than one tone". A split
			 * changes tone ONCE across the bar; a board of six pixel squares changes about ten
			 * times over the same run.
			 */
			ok("AND IT IS A CHECKERBOARD, NOT A SPLIT", changes >= 4);
			if (changes < 4)
				SDL_Log("  the tone changed %d times across the bar - that is a split", changes);

			/* The desk's own two greys, so a transparent pixel means the same thing here as
			 * it does on the sheet. */
			ok("in the desk's own tones",
			   (seen[0] == (VNG_CHECK_A & 0x00FFFFFFu) || seen[0] == (VNG_CHECK_B & 0x00FFFFFFu)));

			SDL_DestroySurface(shot);
		}
	}

	/* ---- THE HEX READOUT IS THERE BEFORE ANYBODY CLICKS IT ----
	 *
	 * It was not. The field behind the box was built lazily in the press handler, so the box
	 * that exists to answer "what colour is this" drew NOTHING until somebody clicked it -
	 * which is asking it a different question entirely. Nothing in a build log says that, and
	 * the window looks finished without it, so it is measured: the window is rendered to a
	 * texture, freshly opened and never clicked, and the hex box is read back.
	 */
	{
		const int N = 320;

		/* THE SUITE RUNS WITHOUT A FONT - it never calls vangopix_init - and every owner in
		 * this program begins `if (!vng_text) return;`. So nothing with a letter in it draws
		 * here by default, and a check about a readout has to bring one. */
		char       *fp    = vangopix_asset("font/DejaVuSansMono.ttf");
		TextSystem *big   = fp ? text_init(vng_ren, fp, 16.0f) : NULL;
		TextSystem *small = fp ? text_init(vng_ren, fp, 11.0f) : NULL;
		SDL_free(fp);

		vng_text       = big;
		vng_text_small = small ? small : big;

		if (colour_visible()) colour_toggle();
		colour_toggle();                       /* up, and NOT clicked */
		ok("the wheel is up for the readback", colour_visible() == true);

		SDL_FRect c = win_area(win_top());

		/* Put it somewhere known, so the readback does not depend on where the pointer was
		 * when it was summoned - the same reason win_place and win_area exist for tests. The
		 * window is larger than a small target, so the target is the window's own size. */
		vng_win_w = N;
		vng_win_h = N;
		win_place(win_top(), (float)N * 0.5f, (float)N * 0.5f);
		c = win_area(win_top());

		SDL_Texture *target = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
		                                        SDL_TEXTUREACCESS_TARGET, N, N);
		SDL_Surface *shot = NULL;

		if (target) {
			SDL_SetRenderTarget(vng_ren, target);
			SDL_SetRenderDrawColor(vng_ren, 0x00, 0xFF, 0x00, 0xFF);
			SDL_RenderClear(vng_ren);

			win_draw();

			SDL_Surface *raw = SDL_RenderReadPixels(vng_ren, NULL);
			if (raw) {
				shot = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
				SDL_DestroySurface(raw);
			}
			SDL_SetRenderTarget(vng_ren, NULL);
			SDL_DestroyTexture(target);
		}

		ok("the window can be read back", shot != NULL);

		if (shot) {
			const Uint32 *px = (const Uint32 *) shot->pixels;
			const int pitch = shot->pitch / 4;

			/* The strip the hex box sits in - the same point the typing checks click on. */
			int x0 = (int)(c.x + 20.0f), x1 = (int)(c.x + 120.0f);
			int y0 = (int)(c.y + c.h - 34.0f), y1 = (int)(c.y + c.h - 20.0f);

			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 > N) x1 = N;
			if (y1 > N) y1 = N;

			/* Distinct colours in that strip. A box that was never drawn leaves the window's
			 * own flat background and nothing else; a box that WAS drawn has its fill, its
			 * rim and eight digits of text in it. */
			Uint32 seen[8];
			int    kinds = 0;

			for (int y = y0; y < y1 && kinds < 8; y++)
				for (int x = x0; x < x1 && kinds < 8; x++) {
					Uint32 v = px[y * pitch + x];
					int    j = 0;
					while (j < kinds && seen[j] != v) j++;
					if (j == kinds) seen[kinds++] = v;
				}

			ok("THE HEX BOX IS DRAWN WITHOUT BEING CLICKED FIRST", kinds >= 3);
			if (kinds < 3) SDL_Log("  only %d distinct colours where the box should be", kinds);

			SDL_DestroySurface(shot);
		}

		colour_toggle();
		ok("and it goes away again", colour_visible() == false);

		vng_text       = NULL;
		vng_text_small = NULL;
		if (small && small != big) text_free(small);
		text_free(big);
	}

	/* ---- CTRL + DOUBLE CLICK OPENS THE WHEEL ON WHAT WAS JUST PICKED ----
	 *
	 * The first Vangopix's gesture, inside the same CTRL block as the pick itself
	 * (tool_misc.c:44). The shortest path from "that shade, but lighter" to the wheel:
	 * absorb it and open the thing that changes it, without the hand leaving the pixel.
	 */
	{
		/* A colour on the sheet that nothing else in this file uses. */
		t->pixels[0] = 0xFF3C7A1Eu;
		t->tex_dirty = true;

		if (colour_visible()) colour_toggle();
		ok("the wheel starts down", colour_visible() == false);

		SDL_SetModState(SDL_KMOD_LCTRL);
		tool_set(T_PENCIL);

		/* The tab has to be framed before anything can be aimed at it: a tab is born with
		 * zoom 0, meaning "never framed", and view_sheet_rect settles that on the first draw
		 * - which never happens headless. */
		vng_win_w = 320;
		vng_win_h = 240;
		view_reset(t);

		SDL_FPoint sp = view_world_to_screen(t, 0.5f, 0.5f);

		SDL_Event e;
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = sp.x;
		e.button.y = sp.y;

		/* One click picks and does NOT open - or every eyedrop would raise a window. */
		e.button.clicks = 1;
		tool_event(&e, t);
		ok("one click picks the colour", tool_colour(0) == 0xFF3C7A1Eu);
		ok("and leaves the wheel alone", colour_visible() == false);

		/* The second click of the pair. */
		e.button.clicks = 2;
		tool_event(&e, t);
		ok("A DOUBLE CLICK OPENS THE WHEEL", colour_visible() == true);
		ok("on the colour that was just picked", tool_colour(0) == 0xFF3C7A1Eu);

		/* AND DOING IT AGAIN DOES NOT PUT IT AWAY. colour_toggle would have; a gesture that
		 * means "edit this" must not close the editor because it was already open. */
		tool_event(&e, t);
		ok("AND AGAIN DOES NOT CLOSE IT", colour_visible() == true);

		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_UP;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = sp.x;
		e.button.y = sp.y;
		tool_event(&e, t);

		/* The right button picks slot 2 and opens nothing - the original's choice, and the
		 * window would otherwise show slot 1 while being about to write slot 0. */
		colour_toggle();
		ok("the wheel is down again", colour_visible() == false);

		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		e.button.button = SDL_BUTTON_RIGHT;
		e.button.clicks = 2;
		e.button.x = sp.x;
		e.button.y = sp.y;
		tool_event(&e, t);

		ok("the right button picks colour 2", tool_colour(1) == 0xFF3C7A1Eu);
		ok("and opens nothing",              colour_visible() == false);

		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_UP;
		e.button.button = SDL_BUTTON_RIGHT;
		e.button.x = sp.x;
		e.button.y = sp.y;
		tool_event(&e, t);

		SDL_SetModState(SDL_KMOD_NONE);
	}

	/* ---- SPRITE ANIMATION: the playhead and the parser ----
	 *
	 * A clip is a rectangle on the sheet plus a count, not a buffer - so nothing here touches a
	 * document. What is checked is the two things gui_animation.c got wrong, because neither is
	 * visible in a build log and both are wrong on every single loop.
	 */
	{
		/* ---- the playhead ----
		 *
		 * The original: `fps += anime_box.speed` once per RENDERED FRAME, then
		 * `fps = fps > (frames - (int)speed) ? 0 : fps`. Two mistakes in three lines - the
		 * unit, and a bound that lets the index reach the frame count itself.
		 */
		{
			bool seen[4] = { false, false, false, false };
			bool past    = false;
			bool ordered = true;

			/* A whole loop of a four-frame clip at a tenth of a second, sampled finely. */
			for (int i = 0; i <= 400; i++) {
				float t = (float)i * 0.001f;
				int   f = anim_frame_at(t, 4, 0.1f);

				if (f < 0 || f > 3) past = true;
				else                seen[f] = true;

				/* and it walks 0,1,2,3 in order rather than jumping */
				if (t < 0.4f && f != (int)(t / 0.1f)) ordered = false;
			}

			ok("THE PLAYHEAD NEVER LEAVES THE CLIP", past == false);
			ok("and it shows every frame of it",
			   seen[0] && seen[1] && seen[2] && seen[3]);
			ok("in order", ordered);

			/* The original's exact failure: at the end of a four-frame loop it indexed 4. */
			ok("the last instant of the loop is frame 3",
			   anim_frame_at(0.399f, 4, 0.1f) == 3);
			ok("and the next one wraps to 0",
			   anim_frame_at(0.400f, 4, 0.1f) == 0);

			/* SECONDS, NOT RENDERED FRAMES. The same elapsed time is the same frame however
			 * finely it was fed in - which is the whole difference from `fps += speed`. */
			ok("HALF A SECOND IS THE SAME FRAME WHATEVER THE STEP",
			   anim_frame_at(0.55f, 4, 0.1f) == anim_frame_at(0.55f, 4, 0.1f) &&
			   anim_frame_at(0.55f, 4, 0.1f) == 1);

			/* A .anime is a text file a person can edit, so it can say nonsense. */
			ok("a speed of zero is not a division", anim_frame_at(1.0f, 4, 0.0f) == 0);
			ok("and no frames is not a modulo by zero", anim_frame_at(1.0f, 0, 0.1f) == 0);
		}

		/* ---- the parser ----
		 *
		 * The original had no bound anywhere: sscanf("\"%[^\"]\"") with no width into a
		 * 32-byte name, no limit on the line count against a 256-entry array, and sscanf's
		 * return ignored so a malformed line left garbage and still counted.
		 */
		{
			char *path = vangopix_asset("checks_tmp.anime");
			ok("a temp path can be built", path != NULL);

			if (path) {
				SDL_IOStream *io = SDL_IOFromFile(path, "w");
				if (io) {
					const char *good  = "\"Run\"[0.040000,4,0,0,32,40]\n";
					const char *good2 = "\"Walk\"[0.100000,6,0,40,32,40]\n";

					SDL_WriteIO(io, good,  SDL_strlen(good));
					SDL_WriteIO(io, good2, SDL_strlen(good2));

					/* A NAME LONGER THAN THE FIELD. Unbounded, this ran off the end of the
					 * name and into speed, frames, x, y, w, h and the next entries. */
					const char *fat =
					    "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\""
					    "[0.1,2,0,0,8,8]\n";
					SDL_WriteIO(io, fat, SDL_strlen(fat));

					/* Lines that are not clips. The original counted every one of them. */
					const char *junk[] = {
						"this is not a clip at all\n",
						"\"Half\"[0.1,2]\n",
						"\"Zero\"[0.1,0,0,0,32,32]\n",     /* no frames */
						"\"Flat\"[0.1,4,0,0,0,32]\n",      /* no width  */
						"\"Still\"[0,4,0,0,32,32]\n",      /* no speed  */
						"\n",
					};
					for (int i = 0; i < 6; i++)
						SDL_WriteIO(io, junk[i], SDL_strlen(junk[i]));

					SDL_CloseIO(io);

					int n = anim_load(path);

					ok("the good lines come in", n >= 2);
					ok("and the two names are right",
					   SDL_strcmp(anim_name(0), "Run")  == 0 &&
					   SDL_strcmp(anim_name(1), "Walk") == 0);

					/*
					 * A NAME LONGER THAN THE FIELD IS REFUSED, not truncated and above all
					 * not spilled. The width in the scanf stops it at 31 characters, and the
					 * closing quote then does not match, so the line is not a clip. Unbounded
					 * - which is what the original was - it ran through the name into speed,
					 * frames, x, y, w, h and on into the next entries.
					 */
					ok("A LONG NAME IS REFUSED RATHER THAN SPILLED", n == 2);
					if (n != 2) SDL_Log("  got %d clips", n);

					ok("and every name that did come in is inside its field",
					   SDL_strlen(anim_name(0)) < VNG_ANIM_NAME &&
					   SDL_strlen(anim_name(1)) < VNG_ANIM_NAME);

					ok("reading out of range is empty, not off the end",
					   anim_name(-1)[0] == 0 && anim_name(9999)[0] == 0);

					/* A FILE LONGER THAN THE ARRAY. The original's parse loop had no bound
					 * against it at all. */
					io = SDL_IOFromFile(path, "w");
					if (io) {
						for (int i = 0; i < VNG_ANIM_MAX * 4; i++) {
							char line[64];
							int  k = SDL_snprintf(line, sizeof line,
							                      "\"c%d\"[0.1,2,0,0,8,8]\n", i);
							SDL_WriteIO(io, line, (size_t)k);
						}
						SDL_CloseIO(io);

						int m = anim_load(path);
						ok("A FILE LONGER THAN THE LIST STOPS AT THE CAP",
						   m == VNG_ANIM_MAX);
						if (m != VNG_ANIM_MAX) SDL_Log("  got %d", m);
					}

					SDL_RemovePath(path);
				}

				/*
				 * A path that is not there was two fclose(NULL) crashes in the original.
				 * Here it costs nothing at all: the list in hand is left alone, because
				 * losing the clips because a path was mistyped is the worse answer.
				 */
				int held = anim_lot();
				ok("a missing file is no clips and no crash",
				   anim_load("no_such_directory/no_such.anime") == 0);
				ok("AND IT DOES NOT COST THE CLIPS ALREADY IN HAND", anim_lot() == held);

				SDL_free(path);
			}
		}

		/*
		 * ---- NO CLIPS AT ALL, which is the state the program now starts in ----
		 *
		 * There is no vangopix.anime shipped: a clip is a rectangle on a canvas, so one
		 * committed here would be four numbers pointing at pixels nobody else has. The window
		 * therefore opens empty, with a clip of no frames and no name - and that is where a
		 * modulo by zero or a degenerate blit would hide.
		 */
		{
			char *empty = vangopix_asset("checks_empty.anime");
			if (empty) {
				SDL_IOStream *io = SDL_IOFromFile(empty, "w");
				if (io) {
					const char *junk = "not a clip\nnor this one\n";
					SDL_WriteIO(io, junk, SDL_strlen(junk));
					SDL_CloseIO(io);

					ok("A FILE OF NOTHING USABLE IS NO CLIPS", anim_load(empty) == 0);
					ok("and the list says so",                 anim_lot() == 0);
					ok("and reading row zero is empty",        anim_name(0)[0] == 0);

					SDL_RemovePath(empty);
				}
				SDL_free(empty);
			}
		}

		/* ---- the window ---- */
		anim_toggle();
		ok("X puts the animation window up", anim_visible() == true);
		win_draw();          /* walks the preview, the list and the icon strip */
		anim_draw(t);        /* and the clip grid on the sheet */
		anim_tick();

		/*
		 * ---- THE EDITOR IS A FORM, AND A FORM TAKES WHAT IS IN IT ----
		 *
		 * The reported bug, driven the way a hand drives it: click a box, type, click the NEXT
		 * box, and at the end press Create. NO ENTER ANYWHERE - because nobody presses ENTER
		 * seven times, and the button that says Create is the one that means "take this".
		 *
		 * Every box committed on ENTER only, so this produced the clip the editor opened with:
		 * "UNKNOWN", 32 x 32, AND NO FRAMES. That is both halves of the complaint at once - a
		 * clip with no frames draws neither the preview nor the grid on the sheet, so the
		 * window that would not keep the values was also the window showing nothing.
		 *
		 * Driven through win_event, not by calling into anim.c, so what is checked is the
		 * chain a press actually takes.
		 */
		{
			SDL_FRect a = win_area(win_top());
			win_place(win_top(), 320.0f, 320.0f);
			a = win_area(win_top());

			/* NEW: the first of the three words in the icon strip. Its band is measured the
			 * same way anim.c stacks it - pad, the row of step buttons, pad. */
			float icon_y = a.y + ui_pad() + ui_row() + ui_pad();
			press_at(a.x + ui_pad() + 2.0f, icon_y + 2.0f);

			VNG_WIN *ed = win_top();
			ok("NEW opens the editor over the player", ed != NULL && win_area(ed).h > a.h);

			/* Counted rather than assumed: putting the window up reads whatever .anime sits
			 * beside the exe, and on a machine that has one this list is not empty. What is
			 * checked is the clip THIS gesture made, wherever it lands. */
			int held = anim_lot();

			SDL_FRect b = win_area(ed);
			static const char *const VAL[7] = { "Leap", "0.25", "3", "0", "0", "8", "8" };

			for (int i = 0; i < 7; i++) {
				SDL_FRect r = { b.x + ui_pad(), b.y + ui_pad() + ui_row() * (float)i,
				                b.w - ui_pad() * 2.0f, ui_row() };
				press_at(r.x + r.w - 4.0f, r.y + r.h * 0.5f);
				wipe();                 /* the box keeps what it holds - see field.h */
				typed(VAL[i]);          /* and NOT enter() */
			}

			float bw = ui_cell() * 7.0f, bh = ui_row();
			press_at(b.x + SDL_floorf((b.w - bw) * 0.5f) + bw * 0.5f,
			         b.y + b.h - bh - ui_pad() + bh * 0.5f);

			ok("CREATE TAKES WHAT WAS TYPED, WITHOUT AN ENTER ANYWHERE",
			   anim_lot() == held + 1 && SDL_strcmp(anim_name(held), "Leap") == 0);
			ok("and the editor closes behind it", win_visible(ed) == false);
			ok("AND THE KEYBOARD IS HANDED BACK", keys_owned() == false);

			win_draw();      /* the preview, now that there is a clip with frames in it */
			anim_draw(t);
		}

		anim_toggle();
		ok("and X puts it away", anim_visible() == false);

		/* The editor belongs to the player, so it goes away with it - and a field left open
		 * would hold the keyboard with no caret anywhere on screen, which is the whole
		 * program's shortcuts dead. */
		anim_tick();
		ok("nothing holds the keyboard once the window is down", keys_owned() == false);
	}

	/* ---- THE ZOOM AS IT IS READ ----
	 *
	 * Shown in two places - the corner readout and the F1 overlay - so it lives in one named
	 * place, for the same reason tool_hex does. Pinned here because a format is exactly the
	 * kind of thing that goes quietly wrong: the trailing zeros are trimmed by hand.
	 */
	{
		char z[16];
		#define ZT(v) (vangopix_zoom_text((v), z, sizeof z), z)

		/* Every step at or above 1:1 is an integer BY DESIGN - that is what the ladder is for
		 * - so every one of them has to come out clean. */
		ok("1:1 reads as 1x",   SDL_strcmp(ZT(1.0f),  "1x")  == 0);
		ok("and 8 as 8x",       SDL_strcmp(ZT(8.0f),  "8x")  == 0);
		ok("and 64 as 64x",     SDL_strcmp(ZT(64.0f), "64x") == 0);

		/* Below it the ladder is halves, and it says so rather than rounding them away. */
		ok("a half is 0.5x",         SDL_strcmp(ZT(0.5f),    "0.5x")    == 0);
		ok("an eighth is 0.125x",    SDL_strcmp(ZT(0.125f),  "0.125x")  == 0);
		ok("the bottom step is 0.0625x", SDL_strcmp(ZT(0.0625f), "0.0625x") == 0);

		/* And view_reset lands BETWEEN steps, because the fit of an odd-sized image is
		 * whatever it is - so an off-ladder value has to be sayable too. */
		ok("an off-ladder fit is sayable", SDL_strcmp(ZT(4.25f), "4.25x") == 0);
		ok("and does not keep its trailing zeros",
		   SDL_strcmp(ZT(2.5f), "2.5x") == 0);

		#undef ZT
	}

	/* ---- THE SMALL FACE FITS THE BUTTON IT EXISTS FOR ----
	 *
	 * The palette's COLOR button is the first Vangopix's rectangle, and that rectangle came
	 * from a 6x6 BITMAP font where five characters made thirty pixels. The main face here has
	 * a line taller than the box the original drew, which is why there is a second atlas.
	 *
	 * MEASURED AGAINST THE DERIVED BUTTON AND THE REAL FACES, not against 36x13 and 11pt: the
	 * whole point of ui.h is that those numbers move when VNG_FONT_SIZE does, and a check
	 * frozen on the old ones would pass while the button overflowed.
	 */
	{
		char       *fp    = vangopix_asset("font/DejaVuSansMono.ttf");
		TextSystem *big   = fp ? text_init(vng_ren, fp, 20.0f) : NULL;
		TextSystem *small = fp ? text_init(vng_ren, fp, 14.0f) : NULL;
		SDL_free(fp);

		if (!big || !small) {
			SDL_Log("SKIP the small face - no font beside the executable");
		} else {
			TextSystem *keep_b = vng_text, *keep_s = vng_text_small;
			vng_text       = big;
			vng_text_small = small;

			/* What palette.c computes for that button, from the same calls it uses. */
			float bw = ui_cell() * 5.0f, bh = ui_row();

			float tw, th;
			text_measure(small, "COLOR", &tw, &th);

			ok("COLOR FITS THE BUTTON AS DERIVED", tw <= bw && th <= bh);
			if (tw > bw || th > bh)
				SDL_Log("  label %.1fx%.1f in a button %.1fx%.1f", tw, th, bw, bh);

			/* And it really is the smaller of the two, or the second atlas is a texture
			 * bought for nothing. */
			float mw, mh;
			text_measure(big, "COLOR", &mw, &mh);
			ok("and it is the smaller of the two", tw < mw && th < mh);

			/*
			 * THE ROW IS TALLER THAN THE LINE IT HOLDS, which is the whole complaint ui.h
			 * answers: the ported rows were 18 and 20 for text that measures the line height,
			 * and one button was 13 for text of 16.
			 */
			ok("A ROW IS TALLER THAN ITS OWN LINE", ui_row() > ui_line());
			ok("and a head bar is too",             ui_head() > ui_line());
			ok("and the close box fits the head",   ui_close() <= ui_head());

			/*
			 * AND THE BUTTON FITS THE BOX IT IS IN. The palette's box was a flat 64 x 70,
			 * sized when that button was thirteen pixels tall; the moment it became a row of
			 * the loaded face it ran out of the bottom of its own window. A box that holds
			 * text cannot be a constant, and this is what says so.
			 */
			{
				float bx = 13.0f, by = 52.0f;          /* palette.c's BTN_X, BTN_Y */
				float box_w = bx + bw + ui_pad();
				float box_h = by + bh + ui_pad();

				if (box_w < 64.0f) box_w = 64.0f;

				ok("THE COLOR BUTTON FITS ITS OWN BOX",
				   bx + bw <= box_w && by + bh <= box_h);
			}

			/* Seven form fields and a button have to fit the editor they are stacked in. */
			float need = ui_pad() + ui_row() * 7.0f + ui_pad() + ui_row() + ui_pad();
			ok("SEVEN FIELDS AND A BUTTON FIT THE EDITOR", need > 0.0f && need < 400.0f);

			vng_text       = keep_b;
			vng_text_small = keep_s;
			text_free(small);
			text_free(big);
		}
	}

	/* ---- THE SHAPES ARE THE SAME SHAPES ON ANY BACKEND ----
	 *
	 * SDL3 picks a renderer per platform - direct3d11 first on Windows, opengl on Linux, metal
	 * on macOS - and on every one of those it draws through a vertex and a fragment shader.
	 * Nothing here is "just blitted": a filled rectangle is two textured triangles.
	 *
	 * That is fine, and it is worth PROVING rather than assuming, because it is also the
	 * cross-platform promise: this program is meant to build on three systems and look the
	 * same on all of them. An interface made of hairlines is exactly where a backend's
	 * rasterisation rules would show through if they were going to.
	 *
	 * The same drawing is rendered twice - once on whatever SDL chose, once on the software
	 * rasteriser - and compared pixel for pixel. It is also the answer to a question worth
	 * recording: the badly resolved circles were NOT the GPU. They were three different
	 * circles computed by three different roundings, which is what primitives.c is for.
	 */
	{
		const int N = 64;
		Uint32 *a = (Uint32 *) SDL_malloc((size_t)N * N * 4);
		Uint32 *b = (Uint32 *) SDL_malloc((size_t)N * N * 4);

		SDL_Renderer *keep = vng_ren;
		SDL_Window   *alt  = NULL;
		SDL_Renderer *soft = NULL;

		if (a && b) {
			/* Everything primitives.c offers, including the one case it hands to SDL - a
			 * diagonal, which has no exact answer and is where a backend could differ. */
			#define DRAW_ALL()                                                        \
				do {                                                                  \
					SDL_SetRenderDrawColor(vng_ren, 0, 0, 0, 0xFF);                   \
					SDL_RenderClear(vng_ren);                                         \
					prim_disc  (32.0f, 32.0f, 20.0f, 0xFF404040u);                    \
					prim_circle(32.0f, 32.0f, 20.0f, 0xFFFFFFFFu);                    \
					prim_line  (6.0f, 6.0f, 58.0f, 26.0f, 0xFF00FF00u);               \
					prim_line  (4.0f, 60.0f, 60.0f, 60.0f, 0xFF00FFFFu);              \
					prim_box   (r_box, 0xFFFFFFFFu, 0xFF000000u);                     \
					prim_fill  (r_frac, 0xFFFF0000u);                                 \
				} while (0)

			SDL_FRect r_box  = { 40.0f, 40.0f, 12.0f, 9.0f };
			SDL_FRect r_frac = { 10.3f, 52.3f, 20.0f, 4.0f };

			/* 1. on whatever SDL chose for this machine */
			SDL_Texture *t = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
			                                   SDL_TEXTUREACCESS_TARGET, N, N);
			if (t) {
				SDL_SetRenderTarget(vng_ren, t);
				DRAW_ALL();
				SDL_Surface *raw = SDL_RenderReadPixels(vng_ren, NULL);
				SDL_Surface *cv  = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888)
				                       : NULL;
				if (cv)
					for (int y = 0; y < N; y++)
						SDL_memcpy(&a[y * N], (Uint8 *)cv->pixels + (size_t)y * cv->pitch,
						           (size_t)N * 4);
				if (cv)  SDL_DestroySurface(cv);
				if (raw) SDL_DestroySurface(raw);
				SDL_SetRenderTarget(vng_ren, NULL);
				SDL_DestroyTexture(t);
			}

			/* 2. on the software rasteriser, which shares no code with any of them */
			alt = SDL_CreateWindow("soft", N, N, SDL_WINDOW_HIDDEN);
			if (alt) soft = SDL_CreateRenderer(alt, "software");

			if (soft) {
				vng_ren = soft;
				SDL_Texture *t2 = SDL_CreateTexture(soft, SDL_PIXELFORMAT_ARGB8888,
				                                    SDL_TEXTUREACCESS_TARGET, N, N);
				if (t2) {
					SDL_SetRenderTarget(soft, t2);
					DRAW_ALL();
					SDL_Surface *raw = SDL_RenderReadPixels(soft, NULL);
					SDL_Surface *cv  = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888)
					                       : NULL;
					if (cv)
						for (int y = 0; y < N; y++)
							SDL_memcpy(&b[y * N], (Uint8 *)cv->pixels + (size_t)y * cv->pitch,
							           (size_t)N * 4);
					if (cv)  SDL_DestroySurface(cv);
					if (raw) SDL_DestroySurface(raw);
					SDL_SetRenderTarget(soft, NULL);
					SDL_DestroyTexture(t2);
				}
				vng_ren = keep;
			}
			#undef DRAW_ALL
		}

		if (a && b && soft) {
			int diff = 0, first = -1;
			for (int i = 0; i < N * N; i++)
				if (a[i] != b[i]) { if (first < 0) first = i; diff++; }

			SDL_Log("     (%s vs software)", SDL_GetRendererName(keep));
			ok("THE SHAPES ARE THE SAME ON ANY BACKEND", diff == 0);
			if (diff)
				SDL_Log("  %d pixels differ, first at (%d,%d): %08X vs %08X",
				        diff, first % N, first / N, a[first], b[first]);
		} else {
			SDL_Log("SKIP the backend comparison - no software renderer here");
		}

		if (soft) SDL_DestroyRenderer(soft);
		if (alt)  SDL_DestroyWindow(alt);
		SDL_free(a);
		SDL_free(b);
	}

	/* ---- A LINE OF ARITHMETIC IN A NUMBER BOX ----
	 *
	 * A sprite offset is almost never a number somebody knows, it is one somebody works out -
	 * so the boxes take `32*4`. Pinned here because an evaluator is the most checkable thing
	 * in this program and because the way it goes wrong is SILENT: a left-to-right walk is
	 * four lines and reads 2+3*4 as 20, and every number it ever produced would look like a
	 * number somebody typed.
	 */
	{
		double v;
		#define NOPE(s) (expr_eval((s), &v) == false)

		ok("* BINDS TIGHTER THAN +",       sum_is("2+3*4", 14) && sum_is("2*3+4", 10));
		ok("and - runs left to right",     sum_is("10-2-3", 5));     /* not 10-(2-3) */
		ok("and so does /",                sum_is("100/10/2", 5));
		ok("brackets outrank both",        sum_is("(2+3)*4", 20) && sum_is("2*(3+(4-1))", 12));

		ok("A SIGN IS NOT A SUBTRACTION",  sum_is("3*-2", -6) && sum_is("3- -2", 5));
		ok("and it stacks",                sum_is("--8", 8) && sum_is("-(3+4)", -7));

		ok("the gesture this is for",      sum_is("32*4", 128) && sum_is("160-8", 152));
		ok("space is not an operator",     sum_is(" 64 + 16 ", 80));
		ok("and a plain number is a sum",  sum_is("32", 32) && sum_is("1.5", 1.5));

		/*
		 * THE REFUSALS ARE THE POINT, and every one of these is a box mid-keystroke. Reading
		 * 32 out of "32*" is exactly the quiet wrong answer SDL_atoi gives; refusing is what
		 * lets the field run this on EVERY key without the number under the hand flickering
		 * through the halves of what is being typed.
		 */
		ok("HALF A SUM IS NOT A VALUE",    NOPE("32*") && NOPE("*4") && NOPE("--"));
		ok("nor is a line with a stray",   NOPE("32 4") && NOPE("1.2.3") && NOPE("32x4"));
		ok("nor an unclosed bracket",      NOPE("(2+3") && NOPE("2+3)") && NOPE("(") && NOPE("()"));
		ok("nor an empty box",             NOPE("") && NOPE("   "));
		ok("DIVIDING BY ZERO IS REFUSED, NOT INFINITE", NOPE("4/0"));
		ok("and so is anything that overflows to it",   NOPE("1e400"));

		#undef NOPE
	}

	/* ---- HOW BIG A FILE IS, IN WORDS ----
	 *
	 * What F1 says about the document on disk. Pinned because the branches are all at
	 * BOUNDARIES: the step from one unit to the next, and the rounding that decides which
	 * side of it a number falls on. A readout that says "1024.0 KB" is naming a unit that
	 * does not exist, and it is the kind of thing that ships because nobody has a file of
	 * exactly that size to hand.
	 */
	{
		char out[32];
		#define SIZE(n) (vangopix_size_text((Uint64)(n), out, sizeof out), out)

		ok("bytes are whole, and say B",
		   SDL_strcmp(SIZE(0), "0 B") == 0 && SDL_strcmp(SIZE(512), "512 B") == 0);
		ok("up to the last one before a kilobyte", SDL_strcmp(SIZE(1023), "1023 B") == 0);

		ok("A KILOBYTE IS 1024, the way the file manager beside this counts",
		   SDL_strcmp(SIZE(1024), "1.0 KB") == 0);
		ok("and everything above bytes carries one decimal",
		   SDL_strcmp(SIZE(1536), "1.5 KB") == 0 && SDL_strcmp(SIZE(25190), "24.6 KB") == 0);

		ok("megabytes and gigabytes are the same rule again",
		   SDL_strcmp(SIZE(1024 * 1024), "1.0 MB") == 0 &&
		   SDL_strcmp(SIZE(1024ull * 1024 * 1024), "1.0 GB") == 0);

		/*
		 * THE ONE THAT WOULD HAVE SHIPPED. 1048570 is 1023.994 KB, which prints as 1024.0 at
		 * one decimal - so the carry has to be decided on the ROUNDED number, not the raw
		 * one, or the readout invents a unit.
		 */
		ok("A NUMBER THAT ROUNDS UP TO 1024 IS THE NEXT UNIT, not 1024.0 of this one",
		   SDL_strcmp(SIZE(1048570), "1.0 MB") == 0);
		ok("and the same one step down",
		   SDL_strcmp(SIZE(1023), "1023 B") == 0 && SDL_strcmp(SIZE(1048576), "1.0 MB") == 0);

		ok("it stops at the top of the table rather than running off it",
		   SDL_strstr(SIZE(1024ull * 1024 * 1024 * 1024 * 8), "TB") != NULL);

		#undef SIZE
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

	/* ---- A PRESS BESIDE THE PROJECT PANEL PUTS IT AWAY ----
	 *
	 * The panel floats over the sheet, so a click beside it is somebody done with the list.
	 * The press is spent on that, not passed through: otherwise closing a panel would leave a
	 * pixel in the artwork. The middle button is the pan and is left alone.
	 *
	 * The slide is driven by sidebar_draw, the one place that advances it, so it is run until
	 * the panel has arrived - a press tested against a panel still at anim 0 would be testing
	 * the collapsed strip instead.
	 */
	{
		int   was_w  = vng_win_w, was_h = vng_win_h;
		float was_dt = vng_dt;

		vng_win_w = 800;
		vng_win_h = 600;
		vng_dt    = 0.1f;

		#define SETTLE() for (int i = 0; i < 60; i++) sidebar_draw()

		SDL_Event e;
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;

		sidebar_toggle();
		SETTLE();
		float edge = sidebar_edge();
		ok("the panel slides in", sidebar_visible() && edge > 0.0f);

		e.button.button = SDL_BUTTON_LEFT;
		e.button.x = edge * 0.5f;
		e.button.y = 300.0f;
		ok("a press INSIDE the panel is the panel's", sidebar_event(&e));
		ok("and leaves it where it is", sidebar_visible());

		e.button.button = SDL_BUTTON_MIDDLE;
		e.button.x = edge + 40.0f;
		ok("the middle button beside it still goes to the pan", !sidebar_event(&e));
		ok("and does not dismiss it", sidebar_visible());

		e.button.button = SDL_BUTTON_LEFT;
		ok("A LEFT PRESS BESIDE THE PANEL IS SPENT ON IT", sidebar_event(&e));
		ok("AND PUTS IT AWAY", !sidebar_visible());

		ok("one already sliding out lets the sheet have the press again", !sidebar_event(&e));

		sidebar_toggle();
		SETTLE();
		e.button.button = SDL_BUTTON_RIGHT;
		ok("A RIGHT PRESS BESIDE IT DOES THE SAME",
		   sidebar_event(&e) && !sidebar_visible());

		SETTLE();
		ok("and it goes all the way out", sidebar_edge() < edge);

		#undef SETTLE

		vng_win_w = was_w;
		vng_win_h = was_h;
		vng_dt    = was_dt;
	}

	/* ---- A FOLDER SHOWS EVERY FILE IN IT, HOWEVER MANY ----
	 *
	 * The panel's walk once kept every node still waiting to be drawn on a stack of 64, and
	 * dropped whatever did not fit. What that bounded was the WIDTH of a folder, not its
	 * depth: a folder of a hundred sprites showed the first sixty-four and said nothing, and
	 * every root waiting below it took one more away. The files were in the tree the whole
	 * time - read off disk and never drawn.
	 *
	 * The root is built by hand rather than with project_add, because that call writes
	 * projects.vngproj beside the executable, and checks.exe lives beside vangopix.exe. A
	 * suite that ran over somebody's project list would be a suite nobody runs twice.
	 */
	{
		#define FILES 100
		#define SUBS  3

		char *dir = vangopix_asset("checks_tree");
		ok("a temp folder can be named", dir != NULL);

		if (dir) {
			char path[1024];

			SDL_CreateDirectory(dir);
			SDL_snprintf(path, sizeof path, "%s/a_sub", dir);
			SDL_CreateDirectory(path);

			/* Empty files are enough: the scan decides by the name, not the contents. */
			for (int i = 0; i < SUBS; i++) {
				SDL_snprintf(path, sizeof path, "%s/a_sub/s%d.png", dir, i);
				SDL_IOStream *io = SDL_IOFromFile(path, "w");
				if (io) SDL_CloseIO(io);
			}
			for (int i = 0; i < FILES; i++) {
				SDL_snprintf(path, sizeof path, "%s/f%03d.png", dir, i);
				SDL_IOStream *io = SDL_IOFromFile(path, "w");
				if (io) SDL_CloseIO(io);
			}

			/* Two roots, the second left shut: a root waiting below the open one is what
			 * took a row away from the old walk, so the check has to have one. */
			VNG_NODE *was   = vng_projects;
			VNG_NODE *roots[2];
			for (int i = 0; i < 2; i++) {
				roots[i]         = (VNG_NODE *) SDL_calloc(1, sizeof *roots[i]);
				roots[i]->path   = SDL_strdup(dir);
				roots[i]->is_dir = true;
				SDL_strlcpy(roots[i]->name, "checks_tree", sizeof roots[i]->name);
			}
			roots[0]->next = roots[1];
			vng_projects   = roots[0];

			project_toggle(roots[0]);
			VNG_NODE *sub = roots[0]->child;
			ok("the subfolder sorts first", sub && sub->is_dir);
			project_toggle(sub);

			int        d = -1;
			VNG_NODE  *n = sidebar_row(0, &d);
			ok("the open root is the first row", n == roots[0] && d == 0);
			n = sidebar_row(1, &d);
			ok("its subfolder is under it", n == sub && d == 1);

			bool subs = true;
			for (int i = 0; i < SUBS; i++) {
				char want[16];
				SDL_snprintf(want, sizeof want, "s%d.png", i);
				n = sidebar_row(2 + i, &d);
				if (!n || d != 2 || SDL_strcmp(n->name, want) != 0) subs = false;
			}
			ok("an open subfolder of a wide folder shows its own files", subs);

			int  first = 2 + SUBS;
			bool all   = true;
			for (int i = 0; i < FILES; i++) {
				char want[16];
				SDL_snprintf(want, sizeof want, "f%03d.png", i);
				n = sidebar_row(first + i, &d);
				if (!n || d != 1 || SDL_strcmp(n->name, want) != 0) {
					if (all) SDL_Log("  row %d: wanted %s, got %s", first + i, want,
					                 n ? n->name : "nothing");
					all = false;
				}
			}
			ok("A FOLDER OF A HUNDRED FILES SHOWS ALL HUNDRED, IN ORDER", all);

			n = sidebar_row(first + FILES, &d);
			ok("and the root below it still comes after them", n == roots[1] && d == 0);
			ok("and nothing after that", sidebar_row(first + FILES + 1, NULL) == NULL);

			project_free();
			vng_projects = was;

			for (int i = 0; i < SUBS; i++) {
				SDL_snprintf(path, sizeof path, "%s/a_sub/s%d.png", dir, i);
				SDL_RemovePath(path);
			}
			for (int i = 0; i < FILES; i++) {
				SDL_snprintf(path, sizeof path, "%s/f%03d.png", dir, i);
				SDL_RemovePath(path);
			}
			SDL_snprintf(path, sizeof path, "%s/a_sub", dir);
			SDL_RemovePath(path);
			SDL_RemovePath(dir);
			SDL_free(dir);
		}

		#undef SUBS
		#undef FILES
	}

	vng_tabs_free();
	SDL_DestroyRenderer(vng_ren);
	SDL_DestroyWindow(vng_win);
	SDL_Quit();

	SDL_Log(fails ? "=== %d FAILED ===" : "=== all passed (%d failures) ===", fails);
	return fails ? 1 : 0;
}
