#include "anim.h"
#include "win.h"
#include "core.h"
#include "keys.h"
#include "field.h"
#include "view.h"
#include "primitives.h"

/*
 * THE NUMBERS ARE THE FIRST VANGOPIX'S, from gui_animation.c.
 *
 *   tool_core.c:153       winmgr_create(menu_animation, "animation", {mouse.x-75, mouse.y-60, 150, 120})
 *   menu_animation:261    winmgr_limit(p, 150, 120)
 *   menu_animation:274-6  the row-step buttons at (cx-20, y+HEAD+8) and (cx+20, y+HEAD+8)
 *   menu_animation:347    the icon strip at y + 56, three 20x20 boxes at +0, +25, +50
 *   menu_animation:350    the work area {x+5, y+79, w-10, h-84}, rows 20 tall
 *   menu_animation:293-4  the zoom step 0.8, floored at 1.0
 *   menu_animation:253    the four preview backgrounds
 *   menu_animation_add:205 the editor forced to 150 x 200
 *   menu_animation_add:221 seven boxes at {x+5, y + 18*i + 25, w-18, 18}
 *   menu_animation_add:237 Create at (x+32, y + h - 20)
 *
 * Written below in the INTERIOR's coordinates, which is one head less in y than the window's.
 * The original's head was 18; win.c measures its own from the loaded face, so anything that
 * was quoted against a window edge is re-derived from the interior rather than assuming 18.
 */
#define OPEN_W    150.0f
#define OPEN_H    120.0f
#define HEAD       18.0f    /* WINMGR_HEAD, only to convert the original's window offsets */

#define STEP_Y      8.0f    /* the row-step buttons: y + HEAD + 8, so 8 into the interior */
#define STEP_DX    20.0f
#define ICON_Y     38.0f    /* the icon strip: y + 56, less the head */
#define ICON_W     20.0f
#define ICON_DX    25.0f
#define LIST_Y     61.0f    /* the work area: y + 79, less the head */
#define LIST_TRIM  66.0f    /* h - 84, expressed against the interior instead */
#define ROW        20.0f

#define ZOOM_STEP   0.8f
#define ZOOM_MIN    1.0f
#define ZOOM_MAX   16.0f    /* the original had no ceiling and the preview is unclipped */

#define EDIT_W    150.0f
#define EDIT_H    200.0f
#define EDIT_ROW   18.0f
#define EDIT_TOP    7.0f    /* 25, less the head */
#define EDIT_PAD    5.0f
#define FIELDS      7

/* The preview's four backgrounds, cycled by the right button. 0xRRGGBBAA there, 0xAARRGGBB
   here - the conversion that has caught this program twice. The last is NOTHING, which is
   what shows the sheet's own transparency through the clip. */
static const Uint32 BG[] = { 0xFF455212u, 0xFF000000u, 0xFFFFFFFFu, 0x00000000u };
#define BG_LOT ((int)(sizeof BG / sizeof BG[0]))

/* --------------------------------------------------------------------------- the model */

typedef struct {
	char  name[VNG_ANIM_NAME];
	float speed;      /* SECONDS per frame - see anim_frame_at */
	int   frames;
	int   x, y, w, h;
} CLIP;

static CLIP list[VNG_ANIM_MAX];
static int  list_lot = 0;
static int  list_top = 0;    /* the first row showing */

/* The clip being played and edited. A COPY and not an index, which is the original's own
   arrangement and the right one: deleting a clip must not leave the player pointing into a
   hole, and a copy simply goes on playing. */
static CLIP box = { "UNKNOWN", 0.1f, 0, 0, 0, 32, 32 };

/* Which entry the editor is writing back to, or -1 for a new one.
 *
 * THE ORIGINAL KEPT A BOOL FOR THIS AND CLEARED IT ONLY ON COMMIT, so closing the editor with
 * its close box left "new" set and the next edit appended a duplicate instead of updating.
 * One value that says both things cannot fall out of step with itself. */
static int  edit_at = -1;

static float clock_s = 0.0f;   /* seconds since the clip started playing */
static int   vert    = 0;      /* the row on the sheet */
static float gap     = 1.0f;   /* the preview's zoom */
static int   bg      = 0;

int anim_lot (void) { return list_lot; }

const char *anim_name (int i)
{
	return (i >= 0 && i < list_lot) ? list[i].name : "";
}

/*
 * WHICH FRAME IS SHOWING AFTER t SECONDS, and the original got this wrong twice.
 *
 * The unit: it did `fps += anime_box.speed` once per RENDERED FRAME, so a clip ran at a speed
 * that depended on the monitor and the load. Seconds are the only unit that does not.
 *
 * The bound: `fps = fps > (frames - (int)speed) ? 0 : fps` tests strictly greater against a
 * limit that is the frame count itself, so fps reached 4.0 on a four-frame clip and (int)fps
 * indexed the FIFTH cell - whatever happened to be to the right on the sheet - for one tick
 * of every loop. A modulo cannot do that.
 */
int anim_frame_at (float t, int frames, float speed)
{
	if (frames < 1) return 0;

	/* A speed of zero is a division, and a .anime is a text file a person can edit. */
	if (!(speed > 0.0f)) return 0;

	int n = (int)(t / speed);

	/* Negative time is not reachable from the clock, but the guard is free and the
	 * alternative is a negative index into a sheet. */
	if (n < 0) n = 0;

	return n % frames;
}

/* ------------------------------------------------------------------------- the .anime */

/*
 * "<name>"[<speed>,<frames>,<x>,<y>,<w>,<h>] - one clip per line, the original's own format,
 * so the files in the first Vangopix's bin/ open here:
 *
 *     "Run"[0.040000,4,0,0,32,40]
 *
 * THE PARSER IS NOT THE ORIGINAL'S, and it had no bounds anywhere: sscanf("\"%[^\"]\"") with
 * no width into a 32-byte name, so a long name overwrote the rest of the struct and the next
 * entries; no limit on the line count against a 256-entry array; and sscanf's return ignored,
 * so a malformed line left garbage and still counted. All three are the same mistake - taking
 * a file's word for how big it is.
 */
static bool parse (const char *line, CLIP *c)
{
	CLIP t;
	SDL_zero(t);

	/* %31[^"] - the width is the whole point. */
	int n = SDL_sscanf(line, " \"%31[^\"]\"[%f,%d,%d,%d,%d,%d]",
	                   t.name, &t.speed, &t.frames, &t.x, &t.y, &t.w, &t.h);

	if (n != 7) return false;                       /* all seven, or it is not a clip */
	if (t.frames < 1 || t.w < 1 || t.h < 1) return false;
	if (!(t.speed > 0.0f)) return false;

	*c = t;
	return true;
}

int anim_load (const char *path)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "r");

	/* The original called fclose on the result without checking it, in both directions. */
	if (!io) return 0;

	/* Read into a fresh list and swap only at the end. A file that turns out to be junk must
	 * not cost the clips already in hand - the same reason the resize builds its new texture
	 * before letting go of the old one. */
	CLIP got[VNG_ANIM_MAX];
	int  lot = 0;

	char line[256];
	while (lot < VNG_ANIM_MAX && vangopix_read_line(io, line, sizeof line))
		if (parse(line, &got[lot])) lot++;

	SDL_CloseIO(io);

	SDL_memcpy(list, got, sizeof(CLIP) * (size_t)lot);
	list_lot = lot;
	list_top = 0;

	if (list_lot > 0) box = list[0];
	return list_lot;
}

static bool anim_save (const char *path)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "w");
	if (!io) return false;

	for (int i = 0; i < list_lot; i++) {
		char line[128];
		int  n = SDL_snprintf(line, sizeof line, "\"%s\"[%f,%d,%d,%d,%d,%d]\n",
		                      list[i].name, list[i].speed, list[i].frames,
		                      list[i].x, list[i].y, list[i].w, list[i].h);
		if (n > 0) SDL_WriteIO(io, line, (size_t)n);
	}
	SDL_CloseIO(io);
	return true;
}

/* Beside the executable, which is where the fonts and the palette list already live: a file
   manager, a shortcut and a terminal each launch from a different directory. */
static void anim_file (bool write)
{
	char *path = vangopix_asset("vangopix.anime");
	if (!path) return;

	if (write) anim_save(path);
	else       anim_load(path);

	SDL_free(path);
}

/* ------------------------------------------------------------------------- the list ops */

/*
 * ONE INDEX CONVENTION, WHICH IS THE FIX FOR THE HEADLINE BUG. The original selected with
 * `iter_select + j` - the scrolled index - and then deleted and moved with a bare `j`, the
 * screen row. The two agree only while the list is scrolled to the top, so the moment it was
 * not, deleting a row removed a different clip. Everything below takes the LIST index, and
 * the one place that turns a screen row into one is the event handler.
 */
static void list_del (int i)
{
	if (i < 0 || i >= list_lot) return;

	for (int j = i; j < list_lot - 1; j++) list[j] = list[j + 1];
	list_lot--;
}

static void list_up (int i)
{
	if (i <= 0 || i >= list_lot) return;

	CLIP t = list[i - 1];
	list[i - 1] = list[i];
	list[i]     = t;
}

/* ------------------------------------------------------------------------- the editor */

static VNG_WIN   *edit_win = NULL;
static VNG_FIELD *edit_box[FIELDS];

/* n s f x y w h - the original's order, and the order the .anime line is written in. */
static const char *const EDIT_LABEL[FIELDS] = { "n:", "s:", "f:", "x:", "y:", "w:", "h:" };

static void edit_text (int i, char *dst, size_t cap)
{
	switch (i) {
	case 0: SDL_strlcpy(dst, box.name, cap);                 break;
	case 1: SDL_snprintf(dst, cap, "%g", box.speed);         break;
	case 2: SDL_snprintf(dst, cap, "%d", box.frames);        break;
	case 3: SDL_snprintf(dst, cap, "%d", box.x);             break;
	case 4: SDL_snprintf(dst, cap, "%d", box.y);             break;
	case 5: SDL_snprintf(dst, cap, "%d", box.w);             break;
	default: SDL_snprintf(dst, cap, "%d", box.h);            break;
	}
}

/* What ENTER in one box means. It writes only that box, so a half-typed form is never
   committed as a whole - the original rebuilt the whole clip from all seven buffers on every
   frame, which is why a stray keystroke could resize a clip mid-play. */
static void edit_done (const char *text, void *ctx)
{
	int i = (int)(intptr_t)ctx;

	switch (i) {
	case 0: SDL_strlcpy(box.name, text, sizeof box.name);           break;
	case 1: box.speed  = (float)SDL_atof(text);                     break;
	case 2: box.frames = SDL_atoi(text);                            break;
	case 3: box.x      = SDL_atoi(text);                            break;
	case 4: box.y      = SDL_atoi(text);                            break;
	case 5: box.w      = SDL_atoi(text);                            break;
	default: box.h     = SDL_atoi(text);                            break;
	}

	/* A clip with no frames or no size is a division and a degenerate blit. The field takes
	 * what was typed; the model takes what can be played. */
	if (box.frames < 1)      box.frames = 1;
	if (box.w < 1)           box.w = 1;
	if (box.h < 1)           box.h = 1;
	if (!(box.speed > 0.0f)) box.speed = 0.1f;
}

static SDL_FRect edit_rect (SDL_FRect a, int i)
{
	SDL_FRect r = { a.x + EDIT_PAD, a.y + EDIT_TOP + EDIT_ROW * (float)i,
	                a.w - EDIT_PAD * 2.0f, EDIT_ROW };
	return r;
}

static SDL_FRect edit_button (SDL_FRect a)
{
	float w = 60.0f, h = 16.0f;
	SDL_FRect r = { a.x + 32.0f, a.y + a.h - h - 4.0f, w, h };
	return r;
}

/* Puts the clip into the list, at the entry the editor was opened on or as a new one. */
static void edit_commit (void)
{
	int i = (edit_at >= 0 && edit_at < list_lot) ? edit_at
	      : (list_lot < VNG_ANIM_MAX ? list_lot++ : -1);

	if (i < 0) return;   /* the list is full; the original grew past its array instead */

	list[i] = box;
	edit_at = -1;

	if (edit_win) win_show(edit_win, false);
}

static void edit_body (SDL_FRect a, void *ctx)
{
	(void)ctx;

	if (!vng_text) return;

	for (int i = 0; i < FIELDS; i++) {
		char now[VNG_FIELD_MAX];
		edit_text(i, now, sizeof now);

		/* `show` is what a closed box reads out: the clip as it stands. Type into it and it
		 * becomes what is being typed - one box answering in both directions. */
		field_draw(edit_box[i], edit_rect(a, i), EDIT_LABEL[i], now);
	}

	SDL_FRect b = edit_button(a);
	float mx, my;
	SDL_GetMouseState(&mx, &my);

	bool hot = mx >= b.x && my >= b.y && mx < b.x + b.w && my < b.y + b.h;

	prim_fill(b, hot ? 0xFF303030u : 0xFF1C1C1Cu);
	prim_rect(b, 0xFF505050u);

	float tw, th;
	text_measure(vng_text_small, "Create", &tw, &th);
	text_print(vng_text_small, b.x + SDL_floorf((b.w - tw) * 0.5f),
	           b.y + SDL_floorf((b.h - th) * 0.5f), 0xFFFFFFFFu, "Create");
}

static bool edit_event (SDL_FRect a, const SDL_Event *e, void *ctx)
{
	(void)ctx;

	if (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN) return false;
	if (e->button.button != SDL_BUTTON_LEFT)    return false;

	float x = e->button.x, y = e->button.y;

	for (int i = 0; i < FIELDS; i++)
		if (field_press(edit_box[i], edit_rect(a, i), x, y)) return true;

	/* A press anywhere else in the window closes whatever was open WITHOUT committing: a
	 * click is not an ENTER. */
	for (int i = 0; i < FIELDS; i++) field_close(edit_box[i], false);

	SDL_FRect b = edit_button(a);
	if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) {
		edit_commit();
		return true;
	}

	return false;   /* the rest of the window is somewhere to take hold of it */
}

static void edit_open (int at)
{
	edit_at = at;

	if (!edit_win) {
		for (int i = 0; i < FIELDS; i++)
			edit_box[i] = field_make(i == 0 ? VNG_FIELD_TEXT
			                       : i == 1 ? VNG_FIELD_REAL : VNG_FIELD_INT,
			                         edit_done, (void *)(intptr_t)i);

		SDL_FRect  r = { 0.0f, 0.0f, EDIT_W, EDIT_H };
		SDL_FPoint m = { EDIT_W, EDIT_H };

		edit_win = win_open("Set frames", r, m, edit_body, edit_event, NULL);
		win_fixed(edit_win, true);
	}

	float mx, my;
	SDL_GetMouseState(&mx, &my);
	win_place(edit_win, mx, my);
	win_show(edit_win, true);
}

/* -------------------------------------------------------------------------- the player */

static VNG_WIN *win = NULL;

/* The source rect on the sheet: frame n of the clip, on row `vert`. */
static SDL_FRect source (int frame)
{
	SDL_FRect r = { (float)(box.x + box.w * frame),
	                (float)(box.y + box.h * vert),
	                (float)box.w, (float)box.h };
	return r;
}

/* How many whole rows of this clip the sheet holds. The original never asked, so the row
   stepped off the bottom for ever with nothing on screen to say how far it went. */
static int rows_in (VNG_TAB *t)
{
	if (!t || box.h < 1) return 1;

	int n = (t->h - box.y) / box.h;
	return n > 0 ? n : 1;
}

static SDL_FRect step_rect (SDL_FRect a, int right)
{
	float cx = a.x + a.w * 0.5f;
	SDL_FRect r = { cx + (right ? STEP_DX - 10.0f : -STEP_DX - 10.0f),
	                a.y + STEP_Y, 20.0f, 16.0f };
	return r;
}

static SDL_FRect icon_rect (SDL_FRect a, int i)
{
	SDL_FRect r = { a.x + ICON_DX * (float)i, a.y + ICON_Y, ICON_W * 2.0f, ICON_W };
	return r;
}

static SDL_FRect list_rect (SDL_FRect a)
{
	SDL_FRect r = { a.x + 4.0f, a.y + LIST_Y, a.w - 8.0f, a.h - LIST_TRIM };
	if (r.h < ROW) r.h = ROW;
	return r;
}

static void small_label (SDL_FRect r, const char *s, bool hot)
{
	float tw, th;
	if (!vng_text) return;

	text_measure(vng_text_small, s, &tw, &th);
	text_print(vng_text_small, r.x + SDL_floorf((r.w - tw) * 0.5f),
	           r.y + SDL_floorf((r.h - th) * 0.5f),
	           hot ? 0x0080FFFFu : 0xFFFFFFFFu, "%s", s);
}

static void body (SDL_FRect a, void *ctx)
{
	(void)ctx;

	VNG_TAB *t = vng_tab;
	float mx, my;
	SDL_GetMouseState(&mx, &my);

	#define HOT(r) (mx >= (r).x && my >= (r).y && mx < (r).x + (r).w && my < (r).y + (r).h)

	/*
	 * THE PREVIEW SITS ABOVE THE WINDOW, which is where the original put it - and it is the
	 * one thing in this program besides the colour wheel's rim marker that asks to be let out
	 * of its own frame. There is no room for it inside a window 150 x 120 whose three bands
	 * are already spoken for, and a preview scaled sixteen times would not fit any window a
	 * person would want parked.
	 *
	 * The difference from the original is that it is ASKED FOR, for these lines, instead of
	 * being what happens because nothing clipped anything - and that the zoom has a ceiling,
	 * which it did not.
	 */
	if (t && t->tex && box.frames > 0) {
		float pw = (float)box.w * gap, ph = (float)box.h * gap;

		SDL_FRect dst = { a.x + SDL_floorf((a.w - pw) * 0.5f),
		                  a.y - HEAD - ph - 4.0f, pw, ph };
		SDL_FRect src = source(anim_frame_at(clock_s, box.frames, box.speed));

		win_unclip(win);

		/* The chosen background, then the frame over it. The last of the four is NOTHING, so
		 * the desk goes down first and a transparent sprite reads as transparent - which the
		 * original could not show, having no checkerboard. */
		if ((BG[bg] >> 24) < 0xFF) vangopix_desk_rect(dst);
		if ((BG[bg] >> 24) > 0)    prim_fill(dst, BG[bg]);

		SDL_SetTextureScaleMode(t->tex, SDL_SCALEMODE_NEAREST);
		SDL_RenderTexture(vng_ren, t->tex, &src, &dst);

		prim_box(dst, 0xFFFFFFFFu, 0xFF000000u);

		win_clip(win);
	}

	if (!vng_text) return;

	/* The row step. It says which row of how many, which the original never did. */
	SDL_FRect l = step_rect(a, 0), r = step_rect(a, 1);
	small_label(l, "<-", HOT(l));
	small_label(r, "->", HOT(r));

	{
		char n[24];
		SDL_snprintf(n, sizeof n, "%d/%d", vert + 1, rows_in(t));

		float tw, th;
		text_measure(vng_text_small, n, &tw, &th);
		text_print(vng_text_small, a.x + SDL_floorf((a.w - tw) * 0.5f),
		           a.y + STEP_Y + SDL_floorf((16.0f - th) * 0.5f), 0xFF8000FFu, "%s", n);
	}

	/* The icon strip. The original drew three wireframes; these are words in the small face,
	 * because five new glyph paths to say NEW, SAVE and OPEN is five paths to maintain. */
	static const char *const ICON[3] = { "NEW", "SAVE", "OPEN" };
	for (int i = 0; i < 3; i++) {
		SDL_FRect b = icon_rect(a, i);
		small_label(b, ICON[i], HOT(b));
	}

	/* The clip list. */
	SDL_FRect lr = list_rect(a);
	prim_rect(lr, 0xFF303030u);

	int fit = (int)(lr.h / ROW);
	if (fit < 1) fit = 1;

	if (list_top > list_lot - fit) list_top = list_lot - fit;
	if (list_top < 0)              list_top = 0;

	for (int j = 0; j < fit && list_top + j < list_lot; j++) {
		int   i = list_top + j;
		float y = lr.y + (float)j * ROW;

		SDL_FRect row = { lr.x, y, lr.w, ROW };
		bool hot = HOT(row);

		SDL_FRect up = { lr.x, y, ROW, ROW };
		SDL_FRect ex = { lr.x + lr.w - ROW, y, ROW, ROW };

		if (hot) prim_rect(row, 0xFFFFFFFFu);

		char cut[VNG_ANIM_NAME + 4];
		text_fit(vng_text, cut, sizeof cut, list[i].name, lr.w - ROW * 2.0f - 4.0f);

		text_print(vng_text, lr.x + ROW + 2.0f, y + 2.0f,
		           hot ? 0xFFFFFFFFu : 0xFF8000FFu, "%s", cut);

		small_label(up, "^", HOT(up));
		small_label(ex, "x", HOT(ex));
	}

	if (list_lot > fit)
		text_print(vng_text_small, lr.x + lr.w - 10.0f, lr.y + lr.h - ROW,
		           0x808080FFu, "v");

	#undef HOT
}

static bool on_event (SDL_FRect a, const SDL_Event *e, void *ctx)
{
	(void)ctx;

	if (e->type == SDL_EVENT_MOUSE_WHEEL) {
		/* The preview's zoom, the original's 0.8 step - with a ceiling it did not have, since
		 * what it scales is drawn outside the window and over everything. */
		gap = e->wheel.integer_y > 0 ? gap / ZOOM_STEP : gap * ZOOM_STEP;
		if (gap < ZOOM_MIN) gap = ZOOM_MIN;
		if (gap > ZOOM_MAX) gap = ZOOM_MAX;
		return true;
	}

	if (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN) return false;

	float x = e->button.x, y = e->button.y;
	#define HIT(r) (x >= (r).x && y >= (r).y && x < (r).x + (r).w && y < (r).y + (r).h)

	/* The right button cycles the preview's background, the original's own gesture. */
	if (e->button.button == SDL_BUTTON_RIGHT) {
		bg = (bg + 1) % BG_LOT;
		return true;
	}

	SDL_FRect l = step_rect(a, 0), r = step_rect(a, 1);
	if (HIT(l)) { if (vert > 0) vert--; return true; }
	if (HIT(r)) { if (vert < rows_in(vng_tab) - 1) vert++; return true; }

	for (int i = 0; i < 3; i++) {
		if (!HIT(icon_rect(a, i))) continue;

		if (i == 0) edit_open(-1);           /* NEW - and -1 is what says new */
		if (i == 1) anim_file(true);
		if (i == 2) anim_file(false);
		return true;
	}

	SDL_FRect lr = list_rect(a);
	if (HIT(lr)) {
		int fit = (int)(lr.h / ROW);
		int j   = (int)((y - lr.y) / ROW);
		int i   = list_top + j;

		if (fit < 1 || j < 0 || i < 0 || i >= list_lot) return true;

		SDL_FRect up = { lr.x, lr.y + (float)j * ROW, ROW, ROW };
		SDL_FRect ex = { lr.x + lr.w - ROW, lr.y + (float)j * ROW, ROW, ROW };

		/* ALL THREE TAKE THE LIST INDEX. The original selected with the scrolled index and
		 * deleted with the screen row, so a scrolled list deleted the wrong clip. */
		if (HIT(up)) { list_up(i);  return true; }
		if (HIT(ex)) { list_del(i); return true; }

		box     = list[i];
		clock_s = 0.0f;

		/* A second press on the row already playing opens it - the original wanted a double
		 * click, which needs a timer to tell from two presses. This needs nothing. */
		if (edit_at == i) edit_open(i);
		else              edit_at = i;

		return true;
	}

	#undef HIT
	return false;   /* everything else is somewhere to take hold of the window */
}

/* ---------------------------------------------------------------------------- the sheet */

void anim_draw (VNG_TAB *t)
{
	if (!t || !win_visible(win) || box.frames < 1 || box.w < 1 || box.h < 1) return;

	/*
	 * THE GRID ON THE SHEET, which is the best idea in the original: the clip is a rectangle
	 * ON THE DRAWING, so the drawing is where it has to be shown. Without it, defining a clip
	 * is typing four numbers and hoping.
	 *
	 * Two outlines a pixel apart, because one tone always disappears against something - the
	 * sheet can be any colour. prim_box is that idiom, and the original hand-rolled it here
	 * with a second rect at +1.
	 */
	for (int i = 0; i < box.frames; i++) {
		float wx = (float)(box.x + box.w * i);
		float wy = (float)(box.y + box.h * vert);

		SDL_FPoint p = view_world_to_screen(t, wx, wy);
		SDL_FPoint q = view_world_to_screen(t, wx + (float)box.w, wy + (float)box.h);

		SDL_FRect r = { p.x, p.y, q.x - p.x, q.y - p.y };
		prim_box(r, 0xE0FFFFFFu, 0xC0000000u);
	}
}

/* --------------------------------------------------------------------------- the frame */

void anim_toggle (void)
{
	float mx, my;
	SDL_GetMouseState(&mx, &my);

	if (win) {
		bool on = !win_visible(win);

		if (on) { win_place(win, mx, my); clock_s = 0.0f; }
		else if (edit_win) win_show(edit_win, false);   /* the editor belongs to the player */

		win_show(win, on);
		return;
	}

	anim_file(false);   /* whatever is beside the exe, if anything */

	SDL_FRect  r = { 0.0f, 0.0f, OPEN_W, OPEN_H };
	SDL_FPoint m = { OPEN_W, OPEN_H };

	win = win_open("animation", r, m, body, on_event, NULL);
	win_place(win, mx, my);
}

bool anim_visible (void) { return win_visible(win); }

void anim_free (void)
{
	for (int i = 0; i < FIELDS; i++) {
		field_free(edit_box[i]);
		edit_box[i] = NULL;
	}
}

/* The clock, once a frame. It is here and not in the draw because a window that is not
   showing is not playing, and because vng_dt is the program's one clock - clamped, so a
   window drag does not teleport the playhead. */
void anim_tick (void)
{
	if (!win_visible(win)) return;

	clock_s += vng_dt;

	/* Wrapped rather than left to grow, so a window left open all day does not lose the
	 * precision that decides which frame it is on. */
	float loop = box.speed * (float)(box.frames > 0 ? box.frames : 1);
	if (loop > 0.0f)
		while (clock_s >= loop) clock_s -= loop;
}
