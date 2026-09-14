#include "anim.h"
#include "ui.h"
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
 *   menu_animation:274-6  the row-step buttons at (cx-20, y+head+8) and (cx+20, y+head+8)
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
/*
 * THE BANDS ARE STACKED FROM THE FACE, NOT COPIED AS OFFSETS.
 *
 * The original's 8, 56, 79 and its 20px rows were measured against a 6x6 BITMAP font. Carried
 * over literally they gave a list row four pixels taller than the text in it, and an editor
 * row of 18 for a line of 16 - two pixels of slack for seven stacked fields. So the offsets
 * are now the bands added up: pad, a row of buttons, pad, a row of icons, pad, the list. The
 * arrangement is the original's; only the arithmetic is honest about what it is holding.
 */
#define OPEN_W    (ui_cell() * 18.0f)
#define OPEN_H    (STEP_Y + ROW + ui_pad() + ROW + ui_pad() + ROW * 3.0f + ui_pad())
#define STEP_Y     ui_pad()                       /* the row-step buttons */
#define STEP_DX    (ui_cell() * 2.5f)
#define ICON_Y     (STEP_Y + ROW + ui_pad())      /* the icon strip, under them */
#define ICON_W     (ui_cell() * 4.0f)
#define ICON_DX    (ICON_W + ui_pad())
#define LIST_Y     (ICON_Y + ROW + ui_pad())      /* the work area, under that */
#define LIST_TRIM  (LIST_Y + ui_pad())
#define ROW        ui_row()

#define ZOOM_STEP   0.8f
#define ZOOM_MIN    1.0f
#define ZOOM_MAX   16.0f    /* the original had no ceiling and the preview is unclipped */

#define FIELDS      7

/* Seven boxes, a button and the padding around them - added up rather than assumed at 200,
   which is what let the original's rows be shorter than their own text. */
#define EDIT_ROW   ui_row()
#define EDIT_TOP   ui_pad()
#define EDIT_PAD   ui_pad()
#define EDIT_BTN   ui_row()
#define EDIT_W     (ui_cell() * 20.0f)
#define EDIT_H     (EDIT_TOP + EDIT_ROW * (float)FIELDS + ui_pad() + EDIT_BTN + ui_pad())

/* The preview's backgrounds, cycled by the right button - animation_backgrounds in config.txt,
   the original's four by default: green, black, white and NOTHING, where the clip is drawn over
   whatever is behind the preview with no board laid under it. 0xRRGGBBAA in the file and in
   the original, 0xAARRGGBB here - the conversion that has caught this program twice. */
#define BG      (vng_style.anim_bg)
#define BG_LOT  (vng_style.anim_bg_lot)

/* --------------------------------------------------------------------------- the model */

typedef struct {
	char  name[VNG_ANIM_NAME];
	float speed;      /* SECONDS per frame - see anim_frame_at */
	int   frames;
	int   x, y, w, h;
} CLIP;

/*
 * WHAT A CLIP IS BEFORE ANYBODY HAS SAID, named because it is wanted in two places: the value
 * a drawing starts on, and what it goes back to when it has no clips of its own.
 *
 * ONE FRAME AND NOT ZERO, which is what it was. Everything that draws a clip guards on
 * `frames >= 1` - correctly, since the sidecar is a text file - so a player opened before any
 * clip exists showed NO PREVIEW AND NO GRID ON THE SHEET, and nothing on screen said why. The
 * grid is the whole reason a clip is a rectangle on the drawing rather than four numbers, so
 * it has to be there from the first frame the window is up.
 */
static const CLIP CLIP_NEW = { "UNKNOWN", 0.1f, 1, 0, 0, 32, 32 };

/*
 * THE CLIPS OF ONE DRAWING, and every field here is one drawing's.
 *
 * They were file statics: one list for the program, re-read from the sidecar of whatever sheet
 * was on screen when the window was summoned, and then never again. A player left open while
 * the sheet under it changed went on showing the old one's clips over the new one's pixels -
 * and SAVE, which writes beside the sheet ON SCREEN, wrote one image's clips into the other
 * one's .vnganime. Held by the tab, the question of which drawing they belong to cannot be
 * answered wrong, and clips being built for a sheet survive a trip to another one.
 *
 * `box` is the clip being played and edited - a COPY and not an index, which is the original's
 * own arrangement and the right one: deleting a clip must not leave the player pointing into a
 * hole, and a copy simply goes on playing. `edit_at` is which entry the editor writes back to,
 * or -1 for a new one: THE ORIGINAL KEPT A BOOL FOR THIS AND CLEARED IT ONLY ON COMMIT, so
 * closing the editor with its close box left "new" set and the next edit appended a duplicate
 * instead of updating. One value that says both things cannot fall out of step with itself.
 */
struct _vng_anim_ {
	CLIP list[VNG_ANIM_MAX];
	int  lot;
	int  top;        /* the first row showing */
	CLIP box;
	int  vert;       /* the row on the sheet */
	int  edit_at;
};

/* The list row the LAST press in the player landed on, or -1 when it landed anywhere else -
 * half of what makes a double click; see on_event. */
static int  last_row = -1;

/* Which sheet the window last looked at, so the frame a DIFFERENT one comes on screen can let
   go of the editor and the half of a double click that belonged to the other. */
static Uint32 seen_tab = 0;

static float clock_s = 0.0f;   /* seconds since the clip started playing */
static float gap     = 1.0f;   /* the preview's zoom */
static int   bg      = 0;

/*
 * EVERY NUMBER A CLIP CARRIES, HELD TO WHAT CAN BE DRAWN - see VNG_ANIM_FRAMES_MAX.
 *
 * Not a refusal: the form reports every keystroke and the sidecar is a text file, and either
 * can say something absurd on its way to saying something sensible. The clip takes the nearest
 * thing it can play. A rectangle is kept on the scale of the largest sheet this machine can
 * have, which also keeps x + w * frames inside an int.
 */
static void clip_sane (CLIP *c)
{
	int side = vng_tab_side_limit();

	if (c->frames < 1)                   c->frames = 1;
	if (c->frames > VNG_ANIM_FRAMES_MAX) c->frames = VNG_ANIM_FRAMES_MAX;

	if (!(c->speed > 0.0f))              c->speed = 0.1f;
	if (c->speed < VNG_ANIM_SPEED_MIN)   c->speed = VNG_ANIM_SPEED_MIN;
	if (c->speed > VNG_ANIM_SPEED_MAX)   c->speed = VNG_ANIM_SPEED_MAX;

	if (c->w < 1) c->w = 1;
	if (c->h < 1) c->h = 1;
	if (c->w > side) c->w = side;
	if (c->h > side) c->h = side;

	if (c->x < -side) c->x = -side;
	if (c->y < -side) c->y = -side;
	if (c->x >  side) c->x =  side;
	if (c->y >  side) c->y =  side;
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
 *
 * THE TIME IS WRAPPED BEFORE IT IS DIVIDED, so the quotient is always under the frame count:
 * a float divided out to more than an int holds is not a large number when it is converted,
 * it is undefined.
 */
int anim_frame_at (float t, int frames, float speed)
{
	if (frames < 1) return 0;

	/* A speed of zero is a division, and a .anime is a text file a person can edit. */
	if (!(speed > 0.0f)) return 0;

	/* Negative time is not reachable from the clock, but the guard is free and the
	 * alternative is a negative index into a sheet. */
	if (!(t > 0.0f)) return 0;

	float loop = speed * (float)frames;
	if (t >= loop) t = SDL_fmodf(t, loop);

	int n = (int)(t / speed);
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

	/* A clip that IS one but says too much is pulled in, not thrown out - see clip_sane. */
	clip_sane(&t);

	*c = t;
	return true;
}

/*
 * THE CLIPS BELONG TO THE IMAGE, SO THEY ARE KEPT BESIDE IT AND UNDER ITS NAME.
 *
 * hero.png keeps its clips in hero.vnganime, in the same folder. That is the whole of it, and
 * it is what makes LOAD a button rather than a file browser: a clip is four numbers pointing
 * at pixels IN ONE PARTICULAR DRAWING, so there is exactly one file it could mean and nothing
 * to ask. The first Vangopix put up a dialog to go and find it; the dialog was only ever
 * asking a question that has one answer.
 *
 * It replaces a fixed vangopix.anime beside the executable, which was the wrong file in the
 * literal sense: one list of rectangles for every image ever opened, saved over by whichever
 * sheet was on screen last.
 *
 * A sheet that has never been saved has no name to hang it on, and that is why this can fail
 * - the icon strip greys SAVE and LOAD out to say so.
 */
#define ANIM_EXT ".vnganime"

static bool anim_path (VNG_TAB *t, char *dst, size_t cap)
{
	if (!t || !t->path || !t->path[0]) return false;
	if (SDL_strlcpy(dst, t->path, cap) >= cap) return false;

	/* The last dot AFTER the last separator. A dot in a DIRECTORY is not an extension -
	 * C:/my.sprites/hero cut at that dot would write the sidecar outside the folder, and
	 * beside a directory rather than beside the drawing. file.c makes the same distinction
	 * for the same reason. */
	size_t cut = SDL_strlen(dst);
	for (size_t i = cut; i > 0; i--) {
		char c = dst[i - 1];
		if (c == '/' || c == '\\') break;
		if (c == '.') { cut = i - 1; break; }
	}
	dst[cut] = 0;

	return SDL_strlcat(dst, ANIM_EXT, cap) < cap;
}

/* Reads a .anime into `a`. REPLACES the list on success and leaves it alone when the file will
   not open - see anim.h. */
static int read_into (VNG_ANIM *a, const char *path)
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

	SDL_memcpy(a->list, got, sizeof(CLIP) * (size_t)lot);
	a->lot = lot;
	a->top = 0;

	if (a->lot > 0) a->box = a->list[0];
	return a->lot;
}

/*
 * THIS DRAWING'S CLIPS, READ ONCE PER DRAWING - on the first moment anything asks about them,
 * which is the window being up over it.
 *
 * ONCE, and that is the whole reason the set lives on the tab: reading again on every summoning
 * would throw away everything typed since the last SAVE, silently. A drawing whose sidecar does
 * not exist yet is still marked as read, so clips being built for it survive the window being
 * hidden - and now survive another sheet being looked at in between, too.
 */
static VNG_ANIM *anim_of (VNG_TAB *t)
{
	if (!t) return NULL;
	if (t->anim) return t->anim;

	VNG_ANIM *a = (VNG_ANIM *) SDL_calloc(1, sizeof *a);
	if (!a) return NULL;

	a->box     = CLIP_NEW;
	a->edit_at = -1;
	t->anim    = a;

	char path[1024];
	if (anim_path(t, path, sizeof path)) read_into(a, path);
	return a;
}

/* The clips of the drawing on screen - which is the only one the window ever shows. */
static VNG_ANIM *cur (void) { return anim_of(vng_tab); }

void anim_tab_free (VNG_ANIM *a) { SDL_free(a); }

int anim_load (const char *path)
{
	VNG_ANIM *a = cur();
	return (a && path) ? read_into(a, path) : 0;
}

int anim_lot (void)
{
	VNG_ANIM *a = cur();
	return a ? a->lot : 0;
}

const char *anim_name (int i)
{
	VNG_ANIM *a = cur();
	return (a && i >= 0 && i < a->lot) ? a->list[i].name : "";
}

int anim_frames (void)
{
	VNG_ANIM *a = cur();
	return a ? a->box.frames : 0;
}

static bool anim_save (VNG_ANIM *a, const char *path)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "w");
	if (!io) return false;

	for (int i = 0; i < a->lot; i++) {
		char line[128];
		int  n = SDL_snprintf(line, sizeof line, "\"%s\"[%f,%d,%d,%d,%d,%d]\n",
		                      a->list[i].name, a->list[i].speed, a->list[i].frames,
		                      a->list[i].x, a->list[i].y, a->list[i].w, a->list[i].h);
		if (n > 0) SDL_WriteIO(io, line, (size_t)n);
	}
	SDL_CloseIO(io);
	return true;
}

/* SAVE and LOAD, beside the sheet on screen and into ITS clips - the two cannot name
   different drawings any more, which is the bug the per-tab set exists to end. */
static void anim_file (bool write)
{
	VNG_ANIM *a = cur();
	char path[1024];

	if (!a || !anim_path(vng_tab, path, sizeof path)) return;

	if (write) anim_save(a, path);
	else       read_into(a, path);
}

/* ------------------------------------------------------------------------- the list ops */

/*
 * ONE INDEX CONVENTION, WHICH IS THE FIX FOR THE HEADLINE BUG. The original selected with
 * `iter_select + j` - the scrolled index - and then deleted and moved with a bare `j`, the
 * screen row. The two agree only while the list is scrolled to the top, so the moment it was
 * not, deleting a row removed a different clip. Everything below takes the LIST index, and
 * the one place that turns a screen row into one is the event handler.
 */
static void list_del (VNG_ANIM *a, int i)
{
	if (i < 0 || i >= a->lot) return;

	for (int j = i; j < a->lot - 1; j++) a->list[j] = a->list[j + 1];
	a->lot--;
}

static void list_up (VNG_ANIM *a, int i)
{
	if (i <= 0 || i >= a->lot) return;

	CLIP t = a->list[i - 1];
	a->list[i - 1] = a->list[i];
	a->list[i]     = t;
}

/* ------------------------------------------------------------------------- the editor */

static VNG_WIN   *edit_win = NULL;
static VNG_FIELD *edit_box[FIELDS];

/* n s f x y w h - the original's order, and the order the .anime line is written in. */
static const char *const EDIT_LABEL[FIELDS] = { "n:", "s:", "f:", "x:", "y:", "w:", "h:" };

static void edit_text (int i, char *dst, size_t cap)
{
	VNG_ANIM *a = cur();
	if (!a) { if (cap) dst[0] = 0; return; }

	const CLIP *b = &a->box;

	switch (i) {
	case 0: SDL_strlcpy(dst, b->name, cap);                 break;
	case 1: SDL_snprintf(dst, cap, "%g", b->speed);         break;
	case 2: SDL_snprintf(dst, cap, "%d", b->frames);        break;
	case 3: SDL_snprintf(dst, cap, "%d", b->x);             break;
	case 4: SDL_snprintf(dst, cap, "%d", b->y);             break;
	case 5: SDL_snprintf(dst, cap, "%d", b->w);             break;
	default: SDL_snprintf(dst, cap, "%d", b->h);            break;
	}
}

/*
 * WHAT A KEYSTROKE IN ONE BOX MEANS, AND IT MEANS IT AT ONCE.
 *
 * The clip is a RECTANGLE ON THE DRAWING, and the grid is drawn on the sheet for exactly that
 * reason - so typing a width with the grid moving under the hand is the difference between
 * setting a clip and guessing at one. That is the first Vangopix's own behaviour, from the
 * `write settings` block in gui_animation.c:225, and the field reports every key to get it.
 *
 * IT WRITES ONLY THAT BOX, which is the half of the original worth leaving behind: it re-read
 * all seven buffers every frame, so a stray key anywhere resized the clip that was playing.
 */
static void edit_done (const char *text, void *ctx)
{
	VNG_ANIM *a = cur();
	if (!a) return;

	CLIP *b = &a->box;
	int   i = (int)(intptr_t)ctx;

	switch (i) {
	case 0: SDL_strlcpy(b->name, text, sizeof b->name);           break;
	case 1: b->speed  = (float)SDL_atof(text);                    break;
	case 2: b->frames = SDL_atoi(text);                           break;
	case 3: b->x      = SDL_atoi(text);                           break;
	case 4: b->y      = SDL_atoi(text);                           break;
	case 5: b->w      = SDL_atoi(text);                           break;
	default: b->h     = SDL_atoi(text);                           break;
	}

	/* A clip with no frames or no size is a division and a degenerate blit, and one with two
	 * billion frames is a loop that does not end. The field takes what was typed; the model
	 * takes what can be played - see clip_sane. */
	clip_sane(b);
}

/*
 * A FORM TAKES WHAT IS IN IT, and this is the fix for the reported bug.
 *
 * field.h leaves `commit` to the owner because a lone box and a form of seven mean different
 * things by leaving a box. The colour window's hex box is alone: a click elsewhere is a
 * mis-click and dropping the half-typed colour is the kind reading. Seven boxes are a form,
 * and in a form the click that leaves a box is the click that MOVES TO THE NEXT ONE - or the
 * one that presses Create, which is the button that means "take this".
 *
 * Nothing was committing here. Filling in all seven boxes and pressing Create produced the
 * clip the editor opened with - "UNKNOWN", no frames, 32 x 32 - because only ENTER committed
 * and nobody presses ENTER seven times. And a clip with no frames draws neither the preview
 * nor the grid, so the two halves of the complaint were one bug.
 *
 * `except` is the box the press landed on: it is about to be opened and must not be closed and
 * reopened underneath itself. Every other one hands back what it is holding.
 */
static void edit_take (int except)
{
	for (int i = 0; i < FIELDS; i++)
		if (i != except) field_close(edit_box[i], true);
}

/*
 * TAB TO THE NEXT BOX, SHIFT+TAB TO THE ONE BEFORE - the seven-box form the original drove the
 * same way, from `VNG_INPUT(7)` in gui_animation.c:215.
 *
 * CLAMPED AND NOT WRAPPED, which is what the original's __max(reg-1, 0) and __min(reg+1,
 * lot-1) say. TAB off `h:` stays on `h:`; it does not throw the hand back up to `n:`.
 *
 * field.c has already committed the box being left by the time this runs, so `now` is read
 * from a clip that includes whatever was just typed - which is what makes tabbing through the
 * whole form and pressing Create work without an ENTER anywhere.
 */
static void edit_step (int dir, void *ctx)
{
	int i = (int)(intptr_t)ctx + dir;
	if (i < 0 || i >= FIELDS) return;

	char now[VNG_FIELD_MAX];
	edit_text(i, now, sizeof now);

	field_open(edit_box[i], now);
}

static SDL_FRect edit_rect (SDL_FRect a, int i)
{
	SDL_FRect r = { a.x + EDIT_PAD, a.y + EDIT_TOP + EDIT_ROW * (float)i,
	                a.w - EDIT_PAD * 2.0f, EDIT_ROW };
	return r;
}

static SDL_FRect edit_button (SDL_FRect a)
{
	float w = ui_cell() * 7.0f, h = EDIT_BTN;
	SDL_FRect r = { a.x + SDL_floorf((a.w - w) * 0.5f), a.y + a.h - h - ui_pad(), w, h };
	return r;
}

/* Puts the clip into the list, at the entry the editor was opened on or as a new one. */
static void edit_commit (void)
{
	VNG_ANIM *a = cur();
	if (!a) return;

	int i = (a->edit_at >= 0 && a->edit_at < a->lot) ? a->edit_at
	      : (a->lot < VNG_ANIM_MAX ? a->lot++ : -1);

	if (i < 0) return;   /* the list is full; the original grew past its array instead */

	a->list[i] = a->box;
	a->edit_at = -1;

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

	bool hot = ui_hit(b, mx, my);

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

	/* Which box the press landed on, asked BEFORE anything is opened or closed - because
	 * field_open drops whatever else was live, without committing, and it has to find the
	 * others already emptied into the clip by then. */
	int hit = -1;
	for (int i = 0; i < FIELDS; i++)
		if (ui_hit(edit_rect(a, i), x, y)) { hit = i; break; }

	edit_take(hit);

	if (hit >= 0) {
		/* Opened holding what it was READING OUT, which is the clip as it stands - the same
		 * string edit_body hands field_draw. A box that showed 32 and opened blank is how the
		 * width got lost by being looked at. */
		char now[VNG_FIELD_MAX];
		edit_text(hit, now, sizeof now);

		field_press(edit_box[hit], edit_rect(a, hit), x, y, now);
		return true;
	}

	if (ui_hit(edit_button(a), x, y)) {
		edit_commit();
		return true;
	}

	return false;   /* the rest of the window is somewhere to take hold of it */
}

/* The editor going away, by its own close dot or with the player, lets go of the keyboard: a
   box left open would hold it with no caret on screen, and every shortcut would be dead. Not
   committed - the boxes are live, so the clip already holds everything typed. */
static void edit_hidden (void *ctx)
{
	(void)ctx;
	for (int i = 0; i < FIELDS; i++) field_close(edit_box[i], false);
}

static void edit_open (int at)
{
	VNG_ANIM *a = cur();
	if (!a) return;

	a->edit_at = at;

	if (!edit_win) {
		for (int i = 0; i < FIELDS; i++) {
			edit_box[i] = field_make(i == 0 ? VNG_FIELD_TEXT
			                       : i == 1 ? VNG_FIELD_REAL : VNG_FIELD_INT,
			                         edit_done, (void *)(intptr_t)i);

			/* Seven boxes are a form, so they are walkable - see field.h on TAB. */
			field_step(edit_box[i], edit_step);

			/* And the sheet behind them answers every key - see edit_done. */
			field_live(edit_box[i], true);
		}

		SDL_FRect  r = { 0.0f, 0.0f, EDIT_W, EDIT_H };
		SDL_FPoint m = { EDIT_W, EDIT_H };

		edit_win = win_open("Set frames", r, m, edit_body, edit_event, NULL);
		win_fixed(edit_win, true);
		win_on_hide(edit_win, edit_hidden);
	}

	float mx, my;
	SDL_GetMouseState(&mx, &my);
	win_place(edit_win, mx, my);
	win_show(edit_win, true);
}

/* -------------------------------------------------------------------------- the player */

static VNG_WIN *win = NULL;

/* The source rect on the sheet: frame n of the clip, on row `vert`. */
static SDL_FRect source (const VNG_ANIM *a, int frame)
{
	SDL_FRect r = { (float)(a->box.x + a->box.w * frame),
	                (float)(a->box.y + a->box.h * a->vert),
	                (float)a->box.w, (float)a->box.h };
	return r;
}

/* How many whole rows of this clip the sheet holds. The original never asked, so the row
   stepped off the bottom for ever with nothing on screen to say how far it went. */
static int rows_in (VNG_TAB *t, const VNG_ANIM *a)
{
	if (!t || !a || a->box.h < 1) return 1;

	int n = (t->h - a->box.y) / a->box.h;
	return n > 0 ? n : 1;
}

static SDL_FRect step_rect (SDL_FRect a, int right)
{
	float cx = a.x + a.w * 0.5f;
	float w = ui_cell() * 2.5f;
	SDL_FRect r = { cx + (right ? STEP_DX : -STEP_DX - w), a.y + STEP_Y, w, ROW };
	return r;
}

static SDL_FRect icon_rect (SDL_FRect a, int i)
{
	SDL_FRect r = { a.x + ui_pad() + ICON_DX * (float)i, a.y + ICON_Y, ICON_W, ROW };
	return r;
}

static SDL_FRect list_rect (SDL_FRect a)
{
	SDL_FRect r = { a.x + ui_pad(), a.y + LIST_Y,
	                a.w - ui_pad() * 2.0f, a.h - LIST_TRIM };
	if (r.h < ROW) r.h = ROW;
	return r;
}

/* `live` is what says a word can be pressed at all. Greyed rather than hidden: a strip that
   loses an entry when a sheet is unsaved is a strip that moves under the hand, and the answer
   to "why can I not save these" has to be on screen next to the thing that will not. */
static void small_label (SDL_FRect r, const char *s, bool hot, bool live)
{
	float tw, th;
	if (!vng_text) return;

	Uint32 c = !live ? 0x585858FFu : hot ? 0x0080FFFFu : 0xFFFFFFFFu;

	text_measure(vng_text_small, s, &tw, &th);
	text_print(vng_text_small, r.x + SDL_floorf((r.w - tw) * 0.5f),
	           r.y + SDL_floorf((r.h - th) * 0.5f), c, "%s", s);
}

static void body (SDL_FRect a, void *ctx)
{
	(void)ctx;

	VNG_TAB  *t = vng_tab;
	VNG_ANIM *s = cur();
	if (!s) return;

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	#define HOT(r) ui_hit((r), mx, my)

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
	if (t && t->tex && s->box.frames > 0) {
		float pw = (float)s->box.w * gap, ph = (float)s->box.h * gap;

		/* ABOVE the window, UNLESS THERE IS NO ROOM THERE. The head bar is kept on screen
		 * by win.c, not the space a preview wants over it, so a player summoned near the top
		 * - or a clip zoomed up to sixteen times - put the preview off the top edge and the
		 * window went on looking like it was playing nothing. Under the window is the only
		 * other side that is always there. */
		float top = a.y - ui_head() - ph - ui_pad();
		if (top < 0.0f) top = a.y + a.h + ui_pad();

		SDL_FRect dst = { a.x + SDL_floorf((a.w - pw) * 0.5f), top, pw, ph };
		SDL_FRect src = source(s, anim_frame_at(clock_s, s->box.frames, s->box.speed));

		/*
		 * IS THIS FRAME EVEN ON THE SHEET, which is the question nothing was asking.
		 *
		 * A clip is a rectangle STEPPED RIGHT by n*w, so a four-frame clip of 32 starting at
		 * x=32 wants the sheet to be 160 wide - and pixel art is 64. SDL_RenderTexture with a
		 * source rect off the texture DRAWS NOTHING AND RETURNS TRUE: no error, no log, an
		 * empty box for three quarters of every loop. A rect half off is worse than nothing -
		 * SDL clamps the sampling, so it stretches the edge column across the frame and shows
		 * a picture that is not on the sheet anywhere.
		 *
		 * So the frame is either wholly on the sheet or it is not drawn, and the rim says
		 * which. Red rather than a word, because a preview 16 pixels wide has no room for one
		 * and the rim is already there.
		 */
		bool on_sheet = src.x >= 0.0f && src.y >= 0.0f &&
		                src.x + src.w <= (float)t->w && src.y + src.h <= (float)t->h;

		win_unclip(win);

		/*
		 * The chosen background, then the frame over it. The last of the four is NOTHING, and
		 * it means nothing: no fill and no board, so the clip goes straight over whatever is
		 * behind the preview - the drawing, or the desk.
		 *
		 * It used to lay the desk's checkerboard there instead. That shows WHERE a sprite is
		 * transparent, but it is a fourth background and not the absence of one, and what the
		 * choice is for is seeing the clip against what it will really be drawn over. The rim
		 * below still says where the frame ends.
		 */
		if (bg >= BG_LOT) bg = 0;   /* a list that shrank since the last cycle */
		if ((BG[bg] >> 24) > 0) prim_fill(dst, BG[bg]);

		if (on_sheet) {
			SDL_SetTextureScaleMode(t->tex, SDL_SCALEMODE_NEAREST);
			SDL_RenderTexture(vng_ren, t->tex, &src, &dst);
		}

		prim_box(dst, on_sheet ? 0xFFFFFFFFu : 0xFFFF3030u, 0xFF000000u);

		win_clip(win);
	}

	if (!vng_text) return;

	/* The row step. It says which row of how many, which the original never did. */
	SDL_FRect l = step_rect(a, 0), r = step_rect(a, 1);
	small_label(l, "<-", HOT(l), true);
	small_label(r, "->", HOT(r), true);

	{
		char n[24];
		SDL_snprintf(n, sizeof n, "%d/%d", s->vert + 1, rows_in(t, s));

		float tw, th;
		text_measure(vng_text_small, n, &tw, &th);
		text_print(vng_text_small, a.x + SDL_floorf((a.w - tw) * 0.5f),
		           a.y + STEP_Y + SDL_floorf((ROW - th) * 0.5f),
		           style_rgba(vng_style.accent), "%s", n);
	}

	/*
	 * The icon strip. The original drew three wireframes; these are words in the small face,
	 * because five new glyph paths to say NEW, SAVE and LOAD is five paths to maintain.
	 *
	 * LOAD AND NOT OPEN, because it does not open anything: there is no file to go and find,
	 * only this drawing's own sidecar to read back. OPEN is the word for the dialog that is
	 * no longer there.
	 */
	static const char *const ICON[3] = { "NEW", "SAVE", "LOAD" };

	char side[1024];
	bool named = anim_path(t, side, sizeof side);   /* is there an image to hang a file on */

	for (int i = 0; i < 3; i++) {
		SDL_FRect b = icon_rect(a, i);
		small_label(b, ICON[i], HOT(b), i == 0 || named);
	}

	/* The clip list. */
	SDL_FRect lr = list_rect(a);
	prim_rect(lr, vng_style.win_border);

	int fit = (int)(lr.h / ROW);
	if (fit < 1) fit = 1;

	if (s->top > s->lot - fit) s->top = s->lot - fit;
	if (s->top < 0)            s->top = 0;

	for (int j = 0; j < fit && s->top + j < s->lot; j++) {
		int   i = s->top + j;
		float y = lr.y + (float)j * ROW;

		SDL_FRect row = { lr.x, y, lr.w, ROW };
		bool hot = HOT(row);

		SDL_FRect up = { lr.x, y, ROW, ROW };
		SDL_FRect ex = { lr.x + lr.w - ROW, y, ROW, ROW };

		if (hot) prim_rect(row, 0xFFFFFFFFu);

		char cut[VNG_ANIM_NAME + 4];
		text_fit(vng_text, cut, sizeof cut, s->list[i].name, lr.w - ROW * 2.0f - 4.0f);

		text_print(vng_text, lr.x + ROW + ui_pad(),
		           y + SDL_floorf((ROW - ui_line()) * 0.5f),
		           hot ? 0xFFFFFFFFu : style_rgba(vng_style.accent), "%s", cut);

		small_label(up, "^", HOT(up), true);
		ui_close_mark(ex, HOT(ex));
	}

	if (s->lot > fit)
		text_print(vng_text_small, lr.x + lr.w - ui_cell(), lr.y + lr.h - ROW,
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

	VNG_ANIM *s = cur();
	if (!s) return false;

	/* LEAVING THE FORM TAKES WHAT IS IN IT, and this is the other half of that rule: the
	 * editor's boxes are settled by a press in the PLAYER too. Without it, typing a width and
	 * then picking a different clip from the list left the box open holding that width, and
	 * the next press in the editor spent it on the clip that had just been selected instead. */
	edit_take(-1);

	/* Taken and cleared on every press, so only a press that lands on a row can leave one for
	 * the next press to pair with. */
	int row  = last_row;
	last_row = -1;

	float x = e->button.x, y = e->button.y;
	#define HIT(r) ui_hit((r), x, y)

	/* The right button cycles the preview's background, the original's own gesture. */
	if (e->button.button == SDL_BUTTON_RIGHT) {
		bg = (bg + 1) % BG_LOT;
		return true;
	}

	SDL_FRect l = step_rect(a, 0), r = step_rect(a, 1);
	if (HIT(l)) { if (s->vert > 0) s->vert--; return true; }
	if (HIT(r)) { if (s->vert < rows_in(vng_tab, s) - 1) s->vert++; return true; }

	for (int i = 0; i < 3; i++) {
		if (!HIT(icon_rect(a, i))) continue;

		if (i == 0) edit_open(-1);           /* NEW - and -1 is what says new */
		if (i == 1) anim_file(true);         /* SAVE, beside the image and under its name */
		if (i == 2) anim_file(false);        /* LOAD, the same file back */
		return true;
	}

	SDL_FRect lr = list_rect(a);
	if (HIT(lr)) {
		int fit = (int)(lr.h / ROW);
		int j   = (int)((y - lr.y) / ROW);
		int i   = s->top + j;

		if (fit < 1 || j < 0 || i < 0 || i >= s->lot) return true;

		SDL_FRect up = { lr.x, lr.y + (float)j * ROW, ROW, ROW };
		SDL_FRect ex = { lr.x + lr.w - ROW, lr.y + (float)j * ROW, ROW, ROW };

		/* ALL THREE TAKE THE LIST INDEX. The original selected with the scrolled index and
		 * deleted with the screen row, so a scrolled list deleted the wrong clip. */
		if (HIT(up)) { list_up(s, i);  return true; }
		if (HIT(ex)) { list_del(s, i); return true; }

		s->box  = s->list[i];
		clock_s = 0.0f;

		/*
		 * A DOUBLE CLICK OPENS IT, and it takes two things. `clicks` is SDL's count, measured
		 * against the system's own double-click time - the wait a person already set for every
		 * other program on the machine. And both presses on the SAME ROW, because SDL's radius
		 * is 32 pixels and a row is shorter than that: a quick press on one clip and then on the
		 * next is two choices, not a double click.
		 *
		 * The version before this asked only "is this the row already chosen?", so a second
		 * press any time later, a minute later, opened the editor. Choosing a clip to watch it
		 * play and then pressing it again to restart it are both ordinary, and neither means
		 * "edit this".
		 */
		bool twice = e->button.clicks >= 2 && row == i;
		last_row   = i;
		s->edit_at = i;
		if (twice) edit_open(i);

		return true;
	}

	#undef HIT
	return false;   /* everything else is somewhere to take hold of the window */
}

/* ---------------------------------------------------------------------------- the sheet */

void anim_draw (VNG_TAB *t)
{
	if (!t || !win_visible(win)) return;

	VNG_ANIM *s = anim_of(t);
	if (!s || s->box.frames < 1 || s->box.w < 1 || s->box.h < 1) return;

	/*
	 * THE GRID ON THE SHEET, which is the best idea in the original: the clip is a rectangle
	 * ON THE DRAWING, so the drawing is where it has to be shown. Without it, defining a clip
	 * is typing four numbers and hoping.
	 *
	 * Two outlines a pixel apart, because one tone always disappears against something - the
	 * sheet can be any colour. prim_box is that idiom, and the original hand-rolled it here
	 * with a second rect at +1.
	 */
	for (int i = 0; i < s->box.frames; i++) {
		float wx = (float)(s->box.x + s->box.w * i);
		float wy = (float)(s->box.y + s->box.h * s->vert);

		SDL_FPoint p = view_world_to_screen(t, wx, wy);
		SDL_FPoint q = view_world_to_screen(t, wx + (float)s->box.w, wy + (float)s->box.h);

		/* A cell wholly past the right of the window, and every one after it, is not seen. */
		if (p.x > (float)vng_win_w) break;

		SDL_FRect r = { p.x, p.y, q.x - p.x, q.y - p.y };
		prim_box(r, 0xE0FFFFFFu, 0xC0000000u);
	}
}

/* --------------------------------------------------------------------------- the frame */

/* THE EDITOR BELONGS TO THE PLAYER, so it goes when the player goes - by X, or by the player's
   own close dot, which used to leave the editor parked on its own. */
static void player_hidden (void *ctx)
{
	(void)ctx;
	if (edit_win) win_show(edit_win, false);
}

void anim_toggle (void)
{
	float mx, my;
	SDL_GetMouseState(&mx, &my);

	if (win) {
		bool on = !win_visible(win);

		if (on) { win_place(win, mx, my); clock_s = 0.0f; }

		win_show(win, on);
		return;
	}

	SDL_FRect  r = { 0.0f, 0.0f, OPEN_W, OPEN_H };
	SDL_FPoint m = { OPEN_W, OPEN_H };

	win = win_open("animation", r, m, body, on_event, NULL);
	win_on_hide(win, player_hidden);
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
	/*
	 * ANOTHER SHEET ON SCREEN IS ANOTHER SET OF CLIPS, and the editor belongs to a clip of the
	 * old one: the entry it writes back to is an index into THAT list. It is let go of, and so
	 * is half a double click on a row of the other list.
	 */
	Uint32 id = vng_tab ? vng_tab->id : 0;
	if (id != seen_tab) {
		seen_tab = id;
		last_row = -1;
		clock_s  = 0.0f;
		for (int i = 0; i < FIELDS; i++) field_close(edit_box[i], false);
		if (edit_win) win_show(edit_win, false);
	}

	if (!win_visible(win)) return;

	VNG_ANIM *s = cur();
	if (!s) return;

	clock_s += vng_dt;

	/* Wrapped rather than left to grow, so a window left open all day does not lose the
	 * precision that decides which frame it is on. With fmod and not by subtraction: a loop
	 * shorter than one step of a float at this clock is a subtraction that changes nothing,
	 * and a while loop around it that never ends. */
	float loop = s->box.speed * (float)(s->box.frames > 0 ? s->box.frames : 1);
	if (loop > 0.0f && clock_s >= loop) clock_s = SDL_fmodf(clock_s, loop);
}
