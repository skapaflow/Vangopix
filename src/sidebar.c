#include "sidebar.h"
#include "ui.h"
#include "project.h"
#include "tabbar.h"
#include "tabs.h"
#include "core.h"
#include "primitives.h"
#include "glyph.h"

#define BAR_W     (ui_cell() * 32.0f)
#define PAD       (ui_pad() * 2.0f)
#define INDENT    (ui_pad() * 3.0f)
#define CLOSE_W   ui_close()

/*
 * THE FOLDER MARK'S COLUMN, and how big the drawing inside it is.
 *
 * Wide enough for the open folder, which is the bigger of the two: its front panel reaches
 * three units further right than the box does, so a column measured on the shut one would
 * have the open one leaning into the name beside it.
 *
 * The scale is what puts a shape drawn on vector.ini's grid of roughly -8..+10 into a row
 * measured from the face. It is stated once here rather than at the call, because the column
 * and the drawing in it are one measurement seen from two sides.
 */
#define MARK_W    (ui_cell() * 2.0f)
#define MARK_S    (MARK_W / 21.0f)

/*
 * AND AIR BETWEEN THE MARK AND THE NAME.
 *
 * The column is measured on the OPEN folder, whose front panel reaches three units further
 * right than its box - so with the name starting where the column ends, an open folder had
 * about half a pixel of daylight and read as one shape with the word beside it. The shut one
 * looked fine, which is the worst kind of wrong: it only appeared when a folder was expanded.
 *
 * A separate number rather than a wider column, because they are two different facts. The
 * column is how much room the drawing needs; this is how far apart two things sit, and that
 * is what ui_pad already means everywhere else in this program.
 */
#define MARK_GAP  ui_pad()
#define MAX_ROWS  4096   /* what a person can scroll through before giving up and using
                          * a file manager. A deep tree past this is truncated, not
                          * crashed - see rows_build. */

static bool  visible = false;
static float scroll  = 0.0f;

/*
 * How far in the panel is: 0 fully out to the left, 1 fully in. `visible` is only where
 * it is HEADED - every position on screen comes from `anim`, so a click during the slide
 * lands on the panel where the eye sees it and not where it will end up.
 *
 * The step is an exponential lerp rather than a fixed increment per frame, because the
 * loop is vsynced and a 144Hz monitor would otherwise open the panel twice as fast as a
 * 60Hz one. Feeding the elapsed time through 1 - e^(-RATE*dt) makes the curve a function
 * of seconds, so the panel takes the same time to arrive on any machine.
 *
 * A lerp only ever approaches its target, so the last half pixel is snapped: without it
 * `anim` never reaches 0, the panel keeps a sliver on screen for ever, and the sidebar
 * would go on eating clicks along the left edge after it was dismissed.
 */
#define ANIM_RATE  16.0f   /* 90% of the way in about 150ms, which is where the eye calls
                            * it arrived; below 10 the panel drags, above 25 the slide is
                            * over before it reads as motion at all */
#define ANIM_SNAP   0.002f /* half a pixel of 260 - past this nothing more is visible */

static float anim = 0.0f;

static void anim_step (void)
{
	float target = visible ? 1.0f : 0.0f;

	anim += (target - anim) * (1.0f - SDL_expf(-ANIM_RATE * vng_dt));
	if (SDL_fabsf(target - anim) < ANIM_SNAP) anim = target;
}

/* Where the left edge of the panel is right now: -BAR_W when out, 0 when in. Every
 * coordinate in this file, drawn or tested, is measured from it. */
static float slide (void) { return (anim - 1.0f) * BAR_W; }

/*
 * Where the top of the panel is: under the tab bar while the bar is up, at the top of
 * the window while it is down. Both float over the sheet and both claim the same corner,
 * so one of them has to give way, and the bar is the one that spans the whole width.
 *
 * It is NOT lerped, deliberately. The bar itself appears and disappears in a single
 * frame; easing the sidebar into place while the bar snaps would open a gap of
 * checkerboard between the two for the length of the ease. They move together because
 * they are one edge.
 */
static float top_y (void) { return tabbar_height(); }

typedef struct { VNG_NODE *n; int depth; } ROW;

static ROW rows[MAX_ROWS];
static int row_lot = 0;

/* The root whose [x] the button went down on. Same contract as the tab bar: the removal
 * only fires if the button comes back up over the same [x]. */
static VNG_NODE *close_armed = NULL;

/*
 * DRAGGING A ROOT UP AND DOWN THE LIST - the tab row's gesture, turned on its side, and the
 * two states are the tab row's two for the same reason.
 *
 * `held` is the root the button went down on, which is still only a CLICK. `dragging` is that
 * click having travelled past the slop. Without the split, every press on a folder would
 * reorder the list by a pixel of hand tremor.
 *
 * The slop earns more here than it does on the tab row, because a press on a root already
 * MEANS something: it expands the folder. A tab is selected on press and selecting twice is
 * the same as selecting once, so the tab row can afford to act immediately. Expanding twice
 * is collapsing, so a root has to wait for the button to come up to know which gesture it
 * was - see the release below.
 */
#define DRAG_SLOP  4.0f

static VNG_NODE *held     = NULL;
static bool      dragging = false;
static float     press_y  = 0.0f;
static float     grab_dy  = 0.0f;   /* where inside the row the hand took hold */

void sidebar_toggle (void)
{
	visible = !visible;

	/*
	 * SUMMONING IT IS ASKING WHAT IS ON DISK, so what is on screen is read again - see
	 * project_refresh. Putting it away reads nothing: the answer would be drawn on a panel
	 * that is sliding off, and the next summoning asks again anyway.
	 *
	 * Here rather than in project.c because this is the GESTURE. The model has no idea it is
	 * being looked at; it knows about directories, and being looked at is the panel's half of
	 * the arrangement - the same split every file in this pair keeps.
	 */
	if (visible) project_refresh();
	/* A press that was armed on an [x] is dropped along with the panel: the button will
	 * come up somewhere the sidebar is no longer listening, and an arming that outlives
	 * its panel fires on the next release, in the next session of it. */
	close_armed = NULL;
	held        = NULL;
	dragging    = false;
}

/* Reports where the panel is HEADED, not where it is. What asks - a folder dropped on a
 * hidden sidebar - wants to know whether to summon it, and a panel already on its way in
 * must not be toggled back out. */
bool sidebar_visible (void) { return visible; }

/*
 * THE COLLAPSED STRIP - see sidebar.h on why it is only ever offered on an empty desk.
 *
 * Wide enough for the folder mark and its padding, and no wider: it is a handle, not a panel.
 * `vng_tab` is asked directly rather than being told by core.c, because "is there a document"
 * is not a fact core.c owns any better than this file does, and a flag set once a frame is a
 * flag that can be left set.
 */
#define STUB_W  (ui_cell() * 2.0f + ui_pad() * 4.0f)

/* Declared here because the strip is measured against the panel's own row height and the
   panel's measurements come further down - one line beats moving a block to suit an order
   the file already has a reason for. */
static float row_h (void);

static bool stub_up (void) { return vng_tab == NULL; }

static SDL_FRect stub_rect (void)
{
	float t = top_y();
	SDL_FRect r = { 0.0f, t, STUB_W, (float)vng_win_h - t };
	return r;
}

static void stub_draw (void)
{
	SDL_FRect r = stub_rect();

	float mx, my;
	SDL_GetMouseState(&mx, &my);
	bool hot = mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h;

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	SDL_SetRenderDrawColor(vng_ren, 0x14, 0x14, 0x14, hot ? 0xF0 : 0xA0);
	SDL_RenderFillRect(vng_ren, &r);

	/* The same line down the right edge the open panel has, so the strip reads as that panel
	 * with almost all of it off screen rather than as a new thing. */
	SDL_FRect edge = { r.x + r.w - 1.0f, r.y, 1.0f, r.h };
	SDL_SetRenderDrawColor(vng_ren, 0x30, 0x30, 0x30, 0xFF);
	SDL_RenderFillRect(vng_ren, &edge);

	/*
	 * The mark sits where the panel's FIRST ROW would be, not in the middle of the strip: it
	 * is the top of the list peeking out, and a folder floating at the centre of a tall bar
	 * would be a button that happens to live in a stripe.
	 */
	float h = row_h();
	glyph_draw(GLYPH_DIR_SHUT, r.x + r.w * 0.5f, r.y + PAD + h * 0.5f + MARK_S,
	           0.0f, MARK_S, hot ? 0xFFFFFFFFu : 0x9A9A9AFFu);
}

float sidebar_edge (void)
{
	if (anim > 0.0f)  return slide() + BAR_W;
	if (stub_up())    return STUB_W;
	return 0.0f;
}

static float row_h (void)
{
	float h = text_line_height(vng_text);
	return h > 1.0f ? h + 3.0f : 17.0f;
}

/*
 * Flattens the visible part of the tree into rows, in display order.
 *
 * Rebuilt on every event and every frame rather than cached, because it only walks what
 * is EXPANDED - a collapsed project costs one row no matter how much is under it. A
 * cache would have to be invalidated by every toggle, every add and every remove, and
 * the walk it saves is a few hundred pointer steps.
 */
static void rows_build (void)
{
	row_lot = 0;

	VNG_NODE *stack[64];
	int       depth[64];
	int       top = 0;

	/* Explicit stack rather than recursion: the depth of a directory tree is decided by
	 * whatever folder was dropped in, and a deep one must truncate rather than run the
	 * C stack out. */
	for (VNG_NODE *p = vng_projects; p; p = p->next) {
		if (top >= 64) break;
		stack[top] = p;
		depth[top] = 0;
		top++;
	}

	/* The roots went on in order, so they have to come off in order. */
	for (int i = 0; i < top / 2; i++) {
		VNG_NODE *tn = stack[i]; stack[i] = stack[top - 1 - i]; stack[top - 1 - i] = tn;
		int       td = depth[i]; depth[i] = depth[top - 1 - i]; depth[top - 1 - i] = td;
	}

	while (top > 0 && row_lot < MAX_ROWS) {
		VNG_NODE *n = stack[--top];
		int       d = depth[top];

		rows[row_lot].n     = n;
		rows[row_lot].depth = d;
		row_lot++;

		if (n->is_dir && n->open) {
			int first = top;
			for (VNG_NODE *c = n->child; c && top < 64; c = c->next) {
				stack[top] = c;
				depth[top] = d + 1;
				top++;
			}
			for (int i = first; i < first + (top - first) / 2; i++) {
				int j = top - 1 - (i - first);
				VNG_NODE *tn = stack[i]; stack[i] = stack[j]; stack[j] = tn;
				int       td = depth[i]; depth[i] = depth[j]; depth[j] = td;
			}
		}
	}
}

static float content_h (void) { return row_lot * row_h(); }

static void scroll_clamp (void)
{
	float over = content_h() - (vng_win_h - top_y() - PAD * 2.0f);
	if (over < 0.0f) over = 0.0f;
	if (scroll > over)  scroll = over;
	if (scroll < 0.0f)  scroll = 0.0f;
}

static int row_at (float x, float y)
{
	if (x < 0.0f || x >= BAR_W) return -1;

	float local = y - top_y() - PAD + scroll;
	if (local < 0.0f) return -1;

	int i = (int)(local / row_h());
	return (i >= 0 && i < row_lot) ? i : -1;
}

static float row_y (int i) { return top_y() + PAD + i * row_h() - scroll; }

/* Is that node still one of the roots. The list can change under a held hand - a folder
   dropped on the window adds one - and a drag pointed at something no longer in the list
   would relink memory nobody owns. The tab row asks the same question with vng_tab_index. */
static bool is_root (VNG_NODE *n)
{
	for (VNG_NODE *p = vng_projects; p; p = p->next)
		if (p == n) return true;
	return false;
}

/*
 * WHICH ROOT THE HEIGHT y BELONGS TO, as a 0-based index into the roots.
 *
 * Not which ROW - a root that is expanded owns its own row and every row of its subtree, and
 * the block is what a hand is aiming at. Dragging a folder into the middle of another one's
 * open contents means "past that folder", which is the answer this gives.
 *
 * Off the top is the first root and off the bottom is the last, so a drag that runs out of
 * panel keeps meaning something instead of stopping at the edge.
 */
static int root_at (float y)
{
	float h     = row_h();
	float local = y - top_y() - PAD + scroll;

	if (local < 0.0f) return 0;

	int seen = -1;

	for (int i = 0; i < row_lot; i++) {
		if (rows[i].depth == 0) seen++;

		float y0 = i * h;
		if (local >= y0 && local < y0 + h) return seen < 0 ? 0 : seen;
	}

	return seen < 0 ? 0 : seen;
}

bool sidebar_event (const SDL_Event *e)
{
	/*
	 * THE STRIP ANSWERS FIRST, because when it is showing the panel proper is not: at anim 0
	 * every test below misses on its own, so without this the press would fall through to a
	 * desk that has nothing on it to press.
	 */
	if (anim <= 0.0f) {
		if (!stub_up() || e->type != SDL_EVENT_MOUSE_BUTTON_DOWN) return false;

		SDL_FRect r = stub_rect();
		if (e->button.x < r.x || e->button.x >= r.x + r.w ||
		    e->button.y < r.y || e->button.y >= r.y + r.h) return false;

		if (e->button.button == SDL_BUTTON_LEFT) sidebar_toggle();
		return true;   /* either button: it landed on the strip, and nothing under it */
	}

	/* Not `visible`: while the panel is sliding out it still covers pixels, and a click
	 * on what a person can plainly see must not fall through to the sheet. */

	float ox = slide();

	rows_build();

	switch (e->type) {

	case SDL_EVENT_MOUSE_WHEEL: {
		/* Only when the pointer is over the panel. Everywhere else the wheel is the
		 * camera's, and stealing it would make zooming stop working near the left edge
		 * of the window for no reason a person could see. */
		if (e->wheel.mouse_x - ox >= BAR_W) return false;

		scroll -= e->wheel.integer_y * row_h() * 3.0f;
		scroll_clamp();
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		float x = e->button.x - ox, y = e->button.y;

		/*
		 * A PRESS OUTSIDE THE PANEL PUTS IT AWAY, and is spent doing so. The panel floats over
		 * the sheet, so a click beside it is a person done with the list - and letting the same
		 * press through would leave a pixel in the artwork as the price of closing a panel.
		 * Left and right only: the middle button is the pan, and a hand panning to find its
		 * place in the drawing with the list still up is not asking for the list to go.
		 *
		 * Only while it is HEADED in. One already sliding out has been dismissed, and a press on
		 * the sheet beside it is the sheet's again, as it always was.
		 */
		if (x >= BAR_W) {
			if (!visible) return false;
			if (e->button.button != SDL_BUTTON_LEFT &&
			    e->button.button != SDL_BUTTON_RIGHT) return false;
			sidebar_toggle();
			return true;
		}

		if (e->button.button != SDL_BUTTON_LEFT) return true;

		int i = row_at(x, y);
		if (i < 0) return true;      /* empty panel space is still the panel's */

		VNG_NODE *n = rows[i].n;

		/* The [x] only exists on roots: a subfolder is not something the person added,
		 * so there is nothing to take away. */
		if (rows[i].depth == 0 && x >= BAR_W - CLOSE_W - PAD) {
			close_armed = n;
			return true;
		}

		/*
		 * A ROOT IS TAKEN HOLD OF, NOT ACTED ON - see the note by `held`. Expanding it is
		 * what the RELEASE means when the hand did not travel; acting here would collapse
		 * and expand the folder on the way into every drag.
		 */
		if (rows[i].depth == 0) {
			held     = n;
			dragging = false;
			press_y  = y;
			grab_dy  = y - row_y(i);
			return true;
		}

		if (n->is_dir) project_toggle(n);
		else           vng_tab_open(n->path);
		return true;
	}

	/*
	 * MOTION IS CONSUMED ONLY WHILE A ROOT IS HELD, which is win.c's rule for its windows and
	 * for the same reason: a drag that BEGAN on the sheet and crosses this panel - a resize
	 * grip pulled leftward, a pan - must not be cut in half by it. Hover needs none of this;
	 * it reads the pointer directly when drawing.
	 */
	case SDL_EVENT_MOUSE_MOTION: {
		if (!held) return false;

		/* The list can change under the hand. A held root that is no longer in it is a
		 * relink into memory nobody owns. */
		if (!is_root(held)) { held = NULL; dragging = false; return false; }

		float y = e->motion.y;

		if (!dragging && SDL_fabsf(y - press_y) >= DRAG_SLOP) dragging = true;
		if (!dragging) return true;

		/*
		 * THE CENTRE OF THE DRAGGED ROW DECIDES, not the cursor. Taking hold of a row near
		 * its bottom edge would otherwise aim a row early, and the folder would appear to
		 * jump out from under the hand. The tab row settles it the same way.
		 */
		project_move(held, root_at(y - grab_dy + row_h() * 0.5f));
		return true;
	}

	case SDL_EVENT_MOUSE_BUTTON_UP: {
		bool consumed = (held != NULL) || (close_armed != NULL);

		if (close_armed) {
			int i = row_at(e->button.x - ox, e->button.y);
			if (i >= 0 && rows[i].n == close_armed && rows[i].depth == 0 &&
			    e->button.x - ox >= BAR_W - CLOSE_W - PAD)
				project_remove(close_armed);

			close_armed = NULL;
		}

		if (held) {
			/* WHICH GESTURE IT WAS, ANSWERED HERE. A hand that never travelled meant to
			 * open the folder; one that did meant to move it, and the new order becomes a
			 * decision worth writing down only now that the hand has let go - see the note
			 * on project_move. */
			if (dragging) project_save();
			else if (is_root(held)) project_toggle(held);

			held     = NULL;
			dragging = false;
		}

		return consumed;
	}

	default:
		return false;
	}
}

/* ------------------------------------------------------------------ the panel's backdrop */

/*
 * A PICTURE BEHIND THE LIST, stretched to whatever the panel is.
 *
 * THE TWO NUMBERS ARE THE WHOLE THING TO TUNE, so they are named and sit together. They are
 * OPACITY, not transparency: 0.20 means a fifth of the picture and four fifths of the panel
 * under it. Said that way round because it is the number that decides whether the file names
 * on top of it stay readable, which is the only thing this backdrop can get wrong.
 *
 * It fades LEFT TO RIGHT, strongest at the window's edge and weakest where the panel meets
 * the drawing. That is the direction that costs the least: the right-hand end of every row is
 * where a long name runs out and where the [x] sits, so it is the end that most needs to be
 * plain - and the left edge is the one part of this panel that never has anything on it.
 */
#define BG_PNG      "icon/project.png"
#define BG_A_LEFT   1.0f
#define BG_A_RIGHT  0.1f

static SDL_Texture *bg = NULL;
static bool         bg_tried = false;

static void bg_draw (SDL_FRect panel)
{
	if (!bg) {
		/* Read once, and once only even when it fails: a missing file must not be opened
		 * again on every frame for as long as the panel is up. */
		if (bg_tried) return;
		bg_tried = true;

		char *path = vangopix_asset(BG_PNG);
		if (!path) return;

		SDL_Surface *raw = IMG_Load(path);
		SDL_free(path);
		if (!raw) return;

		bg = SDL_CreateTextureFromSurface(vng_ren, raw);
		SDL_DestroySurface(raw);

		if (!bg) return;
		SDL_SetTextureBlendMode(bg, SDL_BLENDMODE_BLEND);

		/* Stretched rather than tiled, so the smooth filter is the honest one - this is a
		 * backdrop, not artwork whose pixels mean something. */
		SDL_SetTextureScaleMode(bg, SDL_SCALEMODE_LINEAR);
	}

	/*
	 * DRAWN AS GEOMETRY AND NOT AS A RECTANGLE, because the alpha has to change ACROSS it.
	 *
	 * SDL_SetTextureAlphaMod is one number for the whole texture, so a gradient made that way
	 * would be a stack of thin strips - a seam per step and a loop per frame. Four vertices
	 * carrying their own alpha hand the ramp to the renderer, which is what interpolating
	 * between vertices is for.
	 */
	const SDL_FColor l = { 1.0f, 1.0f, 1.0f, BG_A_LEFT  };
	const SDL_FColor r = { 1.0f, 1.0f, 1.0f, BG_A_RIGHT };

	float x0 = panel.x, x1 = panel.x + panel.w;
	float y0 = panel.y, y1 = panel.y + panel.h;

	SDL_Vertex v[4] = {
		{ { x0, y0 }, l, { 0.0f, 0.0f } },
		{ { x1, y0 }, r, { 1.0f, 0.0f } },
		{ { x1, y1 }, r, { 1.0f, 1.0f } },
		{ { x0, y1 }, l, { 0.0f, 1.0f } },
	};
	static const int idx[6] = { 0, 1, 2, 0, 2, 3 };

	SDL_RenderGeometry(vng_ren, bg, v, 4, idx, 6);
}

/* ------------------------------------------------------------------- the hover preview */

/*
 * HOW BIG THE PICTURE GETS. Sixteen cells, half the panel's own width - big enough to tell two
 * walk cycles apart and small enough that it does not become the thing on screen.
 */
#define PREV_BOX   (ui_cell() * 16.0f)

/*
 * HOW LONG THE POINTER HAS TO REST BEFORE ANYTHING IS READ OFF DISK.
 *
 * This is the whole reason a preview can afford to open files at all. Sweeping down a list of
 * thirty sprites crosses thirty rows in half a second, and reading each one would be thirty
 * file opens for a person who was on their way somewhere else. A quarter second is under what
 * reads as a wait and over what a moving hand can hold still for.
 */
#define PREV_WAIT  0.25f

static char        *rest_on  = NULL;    /* the path the pointer is resting on, or NULL */
static float        rest_for = 0.0f;    /* how long it has rested there, in seconds */

static SDL_Texture *shot   = NULL;      /* the picture, once it has been read */
static int          shot_w = 0;         /* the FILE's own size, not the texture's - what the */
static int          shot_h = 0;         /* readout says, and what decides the scale */
static bool         shot_tried = false; /* so a file that will not open is not retried at 60Hz */

static void shot_drop (void)
{
	if (shot) SDL_DestroyTexture(shot);
	shot       = NULL;
	shot_w     = 0;
	shot_h     = 0;
	shot_tried = false;
}

void sidebar_free (void)
{
	if (bg) SDL_DestroyTexture(bg);
	bg       = NULL;
	bg_tried = false;

	shot_drop();
	SDL_free(rest_on);
	rest_on = NULL;
}

/*
 * HOW BIG TO DRAW A PICTURE OF w BY h.
 *
 * Two rules, because a photograph and a 32 pixel sprite are opposite problems. Shrinking is
 * free-form: a 4000 pixel wide picture has no business being held to a whole-pixel boundary.
 * ENLARGING GOES IN WHOLE MULTIPLES - a sprite blown up 4.27 times has some rows two screen
 * pixels tall and some three, and a preview of pixel art that misreports which pixels are
 * there is worse than no preview at all. It is the rule the sheet itself follows in core.c.
 */
static void shot_size (int w, int h, float *dw, float *dh)
{
	float box = PREV_BOX;

	if (w < 1 || h < 1) { *dw = box; *dh = box; return; }

	int   big = w > h ? w : h;
	float k   = box / (float)big;

	if (k >= 1.0f) {
		int whole = (int)k;
		if (whole < 1) whole = 1;
		*dw = (float)(w * whole);
		*dh = (float)(h * whole);
		return;
	}

	*dw = SDL_floorf((float)w * k);
	*dh = SDL_floorf((float)h * k);
	if (*dw < 1.0f) *dw = 1.0f;
	if (*dh < 1.0f) *dh = 1.0f;
}

static void shot_read (void)
{
	/* Set FIRST, so a file that cannot be read is not attempted again on the next frame and
	 * every frame after it, for as long as the hand stays still. */
	shot_tried = true;

	SDL_Surface *raw = IMG_Load(rest_on);
	if (!raw) return;

	SDL_Surface *img = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
	SDL_DestroySurface(raw);
	if (!img) return;

	shot_w = img->w;
	shot_h = img->h;

	/*
	 * SHRUNK HERE AND NOT AT DRAW TIME. A photograph is twenty-four megapixels and this box is
	 * a hundred and thirty across; handing the whole thing to the GPU to be scaled down on
	 * every frame is ninety megabytes held for a picture nobody is editing. The full surface
	 * lives on this thread for the length of this function and no longer.
	 */
	float dw, dh;
	shot_size(img->w, img->h, &dw, &dh);

	if (dw < (float)img->w || dh < (float)img->h) {
		SDL_Surface *small = SDL_ScaleSurface(img, (int)dw, (int)dh, SDL_SCALEMODE_LINEAR);
		if (small) { SDL_DestroySurface(img); img = small; }
	}

	shot = SDL_CreateTextureFromSurface(vng_ren, img);
	SDL_DestroySurface(img);

	if (shot) SDL_SetTextureBlendMode(shot, SDL_BLENDMODE_BLEND);
}

/* Which file the pointer is resting on, if it is resting on one at all. NULL for a folder, for
   empty panel space, and for anywhere off the panel. */
static const char *hovered (void)
{
	if (anim <= 0.0f) return NULL;

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	int i = row_at(mx - slide(), my);
	if (i < 0) return NULL;

	VNG_NODE *n = rows[i].n;
	return (n && !n->is_dir && project_is_image(n->name)) ? n->path : NULL;
}

/* Starts the clock over on a different name, and lets go of the picture the last one owned.
   Comparing PATHS and not node pointers: a folder collapsed and reopened builds new nodes at
   whatever addresses happen to be free, and one of them can be the address just released. */
static void rest_track (const char *path)
{
	bool same = (path && rest_on) ? SDL_strcmp(path, rest_on) == 0
	                              : (path == NULL && rest_on == NULL);

	if (!same) {
		SDL_free(rest_on);
		rest_on  = path ? SDL_strdup(path) : NULL;
		rest_for = 0.0f;
		shot_drop();
	}

	if (rest_on) rest_for += vng_dt;
}

void sidebar_hover_draw (void)
{
	rest_track(hovered());

	if (!rest_on)              return;
	if (rest_for < PREV_WAIT)  return;

	if (!shot_tried) shot_read();
	if (!shot)       return;

	float dw, dh;
	shot_size(shot_w, shot_h, &dw, &dh);

	float pad  = ui_pad() * 2.0f;
	float line = vng_text ? ui_small() : 0.0f;

	SDL_FRect card = { 0.0f, 0.0f, dw + pad * 2.0f, dh + pad * 2.0f + line };

	/*
	 * BESIDE THE POINTER, which is where a preview belongs - under it and the hand covers the
	 * thing it was asked about. To the right by default, because the panel it reads from is
	 * against the LEFT edge and there is always room that way; flipped to the left only when
	 * the window is too narrow for that to hold, and pushed back on screen vertically rather
	 * than allowed to hang off an edge.
	 */
	float mx, my;
	SDL_GetMouseState(&mx, &my);

	float step = ui_cell() * 2.0f;

	card.x = mx + step;
	card.y = my - card.h * 0.5f;

	if (card.x + card.w > (float)vng_win_w) card.x = mx - step - card.w;
	if (card.x < 0.0f) card.x = 0.0f;

	if (card.y < 0.0f) card.y = 0.0f;
	if (card.y + card.h > (float)vng_win_h) card.y = (float)vng_win_h - card.h;

	prim_fill(card, 0xF0141414u);
	prim_rect(card, 0xFF505050u);

	SDL_FRect dst = { card.x + pad, card.y + pad, dw, dh };

	/* The checkerboard first: a sprite is transparent everywhere it is not drawn, and a
	 * preview on a flat ground makes an empty sheet and a filled one look the same. */
	vangopix_desk_rect(dst);

	/* Whole multiples on the way up and an exact blit on the way down - shot_read already did
	 * the shrinking - so NEAREST is the honest filter in both directions. */
	SDL_SetTextureScaleMode(shot, SDL_SCALEMODE_NEAREST);
	SDL_RenderTexture(vng_ren, shot, NULL, &dst);

	prim_rect(dst, 0x50FFFFFFu);

	/*
	 * AND HOW BIG IT IS, which is half of what the list is being asked. Two files called
	 * walk_02 and walk_03 look alike in a thumbnail; 32 x 40 against 64 x 40 does not.
	 */
	if (!vng_text) return;

	char size[32];
	SDL_snprintf(size, sizeof size, "%d x %d", shot_w, shot_h);

	float tw, th;
	text_measure(vng_text_small, size, &tw, &th);
	text_print(vng_text_small, card.x + SDL_floorf((card.w - tw) * 0.5f),
	           dst.y + dh + SDL_floorf((pad * 2.0f + line - th) * 0.5f),
	           0xB4B4B4FFu, "%s", size);
}

void sidebar_draw (void)
{
	/* The clock runs whether the panel shows or not - this is the only place per frame
	 * that advances it, so an early return above it would freeze the slide half done. */
	anim_step();

	if (anim <= 0.0f) {
		if (stub_up()) stub_draw();
		return;
	}

	float ox = slide();

	rows_build();
	scroll_clamp();

	float mx, my;
	SDL_GetMouseState(&mx, &my);
	float rx = mx - ox;   /* the pointer, measured from the panel's own left edge */

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	float top = top_y();

	SDL_FRect panel = { ox, top, BAR_W, vng_win_h - top };
	SDL_SetRenderDrawColor(vng_ren, 0x0, 0x0, 0x0, 0xDD	);
	SDL_RenderFillRect(vng_ren, &panel);

	/* Over the ground and under everything else: it is a backdrop, and a backdrop that lands
	 * on top of a file name has stopped being one. */
	bg_draw(panel);

	/* A line down the right edge. The panel is translucent over a checkerboard, and
	 * without it the two greys blur into each other exactly where the edge should be. */
	SDL_FRect edge = { ox + BAR_W - 1.0f, top, 1.0f, vng_win_h - top };
	SDL_SetRenderDrawColor(vng_ren, 0x30, 0x30, 0x30, 0xFF);
	SDL_RenderFillRect(vng_ren, &edge);

	if (!vng_text) return;

	if (row_lot == 0) {
		text_print(vng_text, ox + PAD, top + PAD, 0x707070FF, "drop a folder here");
		return;
	}

	float h = row_h();

	for (int i = 0; i < row_lot; i++) {
		float y = row_y(i);

		/*
		 * THE DRAGGED ROOT FOLLOWS THE HAND. It has already been relinked into its new place
		 * by the time this runs - the list reflows on every motion - so what is drawn here is
		 * only the distance between the slot it now owns and where the hand actually is.
		 * Its open contents stay in the block, which is the honest picture: the row is the
		 * handle, and the folder is already where it is being put.
		 */
		if (dragging && rows[i].n == held) {
			y = my - grab_dy;
			if (y < top) y = top;
			if (y > vng_win_h - h) y = vng_win_h - h;
		}

		/* A row scrolled half under the bar is drawn whole and then painted over:
		 * tabbar_draw runs after this, and its strip is opaque across the width. */
		if (y + h < top) continue;
		if (y > vng_win_h) break;

		VNG_NODE *n   = rows[i].n;
		float     ind = PAD + rows[i].depth * INDENT;
		bool      hot = (rx >= 0.0f && rx < BAR_W && my >= y && my < y + h) && !dragging;

		/* The file that is open right now is named in white. In a folder of thirty
		 * sprites, finding which one is on screen is otherwise a matter of reading the
		 * window title and then reading the list. */
		bool current = (!n->is_dir && vng_tab && vng_tab->path &&
		                SDL_strcmp(vng_tab->path, n->path) == 0);

		bool lifted = (dragging && n == held);

		if (hot || current || lifted) {
			SDL_FRect r = { ox, y, BAR_W - 1.0f, h };
			SDL_SetRenderDrawColor(vng_ren, 0x2E, 0x2E, 0x2E,
			                       (current || lifted) ? 0xFF : 0x80);
			SDL_RenderFillRect(vng_ren, &r);
		}

		/* OPAQUE AND OUTLINED WHILE IT IS BEING CARRIED. It is drawn over rows it does not
		 * belong between, and without an edge it reads as two names overlapping rather than
		 * as one being moved. */
		if (lifted) {
			SDL_FRect r = { ox, y, BAR_W - 1.0f, h };
			SDL_SetRenderDrawColor(vng_ren, 0x80, 0x80, 0x80, 0xFF);
			SDL_RenderRect(vng_ren, &r);
		}

		/* How much room the name has before it would run under the panel edge - or
		 * under the [x], on a root. The [x] only appears on hover, but the space is
		 * reserved whether it is showing or not: a name that fits until the pointer
		 * arrives and then gets overwritten is worse than a name that is always cut. */
		float name_x = ind + MARK_W + MARK_GAP;
		float room   = BAR_W - PAD - name_x - (rows[i].depth == 0 ? CLOSE_W + PAD : 0.0f);

		char label[160];
		text_fit(vng_text, label, sizeof label, n->name, room);

		if (n->is_dir) {
			/*
			 * Hung from the middle of its column, and one unit low: the shapes run from -8
			 * to +6 in y, so their own middle is a unit above the origin they are drawn
			 * around. Putting the ORIGIN in the middle of the row would sit the folder high
			 * in it by exactly that much.
			 */
			glyph_draw(n->open ? GLYPH_DIR_OPEN : GLYPH_DIR_SHUT,
			           ox + ind + MARK_W * 0.5f, y + h * 0.5f + MARK_S,
			           0.0f, MARK_S, hot ? 0x0080ffFF : 0xFFFFFFFF);

			text_print(vng_text, ox + name_x, y + 1.0f,
			           rows[i].depth == 0 ? 0xDCDCDCFF : 0xB4B4B4FF, "%s", label);
		} else {
			text_print(vng_text, ox + name_x, y + 1.0f,
			           current ? 0xFFFFFFFF : 0x909090FF, "%s", label);
		}

		if (rows[i].depth == 0 && hot) {
			SDL_FRect c = { ox + BAR_W - CLOSE_W - PAD, y, CLOSE_W, row_h() };
			ui_close_mark(c, rx >= BAR_W - CLOSE_W - PAD);
		}
	}
}
