#include "palette.h"
#include "ui.h"
#include "win.h"
#include "tool.h"
#include "core.h"
#include "keys.h"
#include "primitives.h"

/*
 * THE NUMBERS ARE THE FIRST VANGOPIX'S, from where it built these three.
 *
 *   core.c                    winmgr_create(gui_palette_box, ..., {5, h, 64, 64+16})
 *                             gui_quickly_palette_box(plt_pos, 20)
 *   gui_palette_box           p->s.w = 64, p->s.h = 64 + WINMGR_HEAD(18)
 *                             the "COLOR" button at (x+13, y+70, 36, 13)
 *                             back circle  centre (x+32, y+h+32), radius 20
 *                             front circle centre (x+22, y+h+21), radius 20
 *                             the grid at (x + w + 4, y + h), cell 20
 *   __palette_grid_draw__     max 48 x 32 cells, minimum side 16*4 = 64
 *                             "%d colors" printed at (g.x + 4, g.y - 16)
 *   __tool_palette_stretch__  three 8px bands: right edge, bottom edge, corner
 *   gui_list_palette          the list window at {150, 100, 200, 200}, rows 20 tall
 *   gui_quickly_palette_box   the grid at (mouse.x - g.w/2, mouse.y + 8)
 *                             the hex centred at (pos.x + g.w/2, pos.y + g.h + 16)
 *
 * THE BOX IS CONTAINED AND THE GRID IS OUTSIDE IT. That is the design, not an accident of the
 * original having nothing that clipped: the window is 64 x 64 and holds two colour discs and
 * one word, and the swatches are a panel of their own hanging off its right edge, stretched by
 * its own three bands. A grid folded into the window would be a window that grows to hold a
 * hundred colours - which is a docked panel with a title bar on it.
 *
 * WHAT THAT COSTS, AND IT IS WORTH SAYING PLAINLY: win.c routes and clips WINDOWS, and the
 * grid is not one. So it takes its own events and draws its own pixels, one rung UNDER the
 * windows in both - offered the event after win_event has refused it, drawn just before
 * win_draw. That is the honest place: a window parked on top covers it and gets the click,
 * exactly as a window parked over anything else does. The one thing it cannot express is
 * sitting at its OWN window's depth rather than under all of them, and that only shows if
 * something is parked between the two.
 */
/*
 * THE BOX IS AS BIG AS WHAT IS IN IT, which is 64 wide for the two discs and then whatever
 * the button below them needs.
 *
 * It was a flat 64 x 70, sized when that button was thirteen pixels tall. The moment the
 * button became a row of the loaded face it ran twelve pixels out of the bottom of its own
 * window - which is this whole exercise in miniature, and the reason a box that holds text
 * may not be a constant.
 */
#define BOX_W    (BTN_X + BTN_W + ui_pad() > 64.0f ? BTN_X + BTN_W + ui_pad() : 64.0f)
#define BOX_H    (BTN_Y + BTN_H + ui_pad())
#define GAP       4.0f    /* p->s.x + p->s.w + 4: from the window's OUTER edge */
#define COUNT_H  ui_line()  /* the band "%d colors" was printed in, at g.y - 16 */
#define CELL     VNG_PAL_CELL    /* gd, passed as 20 at BOTH call sites - 24 now, see palette.h */
#define CHECK    ((int)CELL / 4) /* DIV_NULL(gd, 4) in __generate_tile__: four squares to a cell,
                                  * which is what keeps the board seamless across cells - see
                                  * grid_draw. A cell that is not a multiple of four breaks it. */

/*
 * AND ITS TWO TONES ARE NOT THE DESK'S, WHICH IS A CHOICE THE ORIGINAL MADE AND NOT AN
 * OVERSIGHT. __generate_tile__ carries them with the first attempt still commented out
 * above:
 *
 *     // uint32_t color[2] = {0x000000FF,0xFFFFFFFF};
 *        uint32_t color[2] = {0x303050ff,0x606080ff};
 *
 * against a desk of 0x202020 and 0x303030. Bluer, and the light tone twice the desk's
 * lightest - and black-and-white tried first and thrown away.
 *
 * THE GRID FLOATS OVER THE SHEET AND OVER THE DESK, which is what makes the difference do
 * work rather than decoration: in the desk's own greys an empty cell would be
 * indistinguishable from a hole in the panel, and there would be no telling where the grid
 * ends and the table behind it begins. The tint is what says this board belongs to the
 * palette.
 *
 * 0xRRGGBBAA there and 0xAARRGGBB here, which is the conversion that once turned the
 * checkerboard red.
 */
#define TILE_A  (vng_style.pal_a)   /* palette_board_color in config.txt */
#define TILE_B  (vng_style.pal_b)

#define COLS_OPEN  3      /* 64 / 20, which is what the original's minimum snapped to - kept
                           * as a COUNT of cells, so it stays three however big a cell is */
#define ROWS_OPEN  3

/*
 * ONE, NOT THREE. The original's minimum was the size it opened at, which is why the two were
 * one number here too - but they answer different questions. Three by three is where a grid
 * starts; a single column or a single row is a grid a person has CHOSEN, to lay a ramp out
 * in the order it runs, dark to light, down the side of the drawing. A minimum of three made
 * a strip impossible to ask for.
 */
#define COLS_MIN   1
#define ROWS_MIN   1
#define COLS_MAX  48      /* max_w in __palette_grid_draw__ */
#define ROWS_MAX  32      /* max_h */

#define BAND      8.0f    /* the stretch bands, all three of them */

#define BTN_X    13.0f    /* the "COLOR" button, less the head */
#define BTN_Y    52.0f
#define BTN_W    (ui_cell() * 5.0f)   /* the five characters of COLOR */
#define BTN_H    ui_row()
#define BTN_TEXT "COLOR"

#define RADIUS   20.0f    /* both circles */
#define C1_X     22.0f    /* front: gap + 2,  gap + h + 1  */
#define C1_Y     21.0f
#define C2_X     32.0f    /* back:  gap + 12, gap + h + 12 */
#define C2_Y     32.0f

#define DROP      8.0f    /* mouse.y + 8, where the ALT grid hangs */
#define READ_MID 16.0f    /* pos.y + g.h + 16, where its readout is centred */

#define LIST_X  150.0f    /* winmgr_create(gui_list_palette, ..., {150, 100, 200, 200}) */
#define LIST_Y  100.0f
#define LIST_W  200.0f
#define LIST_H  200.0f
#define ROW      ui_row()   /* font_size in gui_list_palette */

/*
 * THE SHAPE OF THE GRID, AND IT IS THE ONE THING BOTH SUMMONINGS READ.
 *
 * In the first Vangopix this was `plt_pos`, a rectangle the palette window's grid stretched
 * and the ALT grid was handed a copy of - core.c called gui_quickly_palette_box(plt_pos, 20)
 * with the very same rectangle. Stretch the grid beside the window and the grid ALT summons
 * under the hand changed shape with it. That is kept, and it is why both live in this file.
 *
 * Kept as COLUMNS AND ROWS rather than as pixels, which takes out the snapping the original
 * did on every frame (g->w = (int)(g->w / gd) * gd): the grid was never really a rectangle,
 * it was a count of cells with a rectangle written over it. It opens at 3 x 3 because
 * plt_pos opened at 64 x 64 and snapped to that.
 */
static int cols = COLS_OPEN;
static int rows = ROWS_OPEN;

/* --------------------------------------------------------------------------- the model */

struct _vng_pal_ {
	Uint32 c[VNG_PAL_MAX];
	int    lot;
};

/*
 * The scan's hash set. 4096 slots for at most 1536 colours - a load factor of 0.37, which
 * keeps the linear probe short - and zero means empty, which is free because a fully
 * transparent pixel is not a colour and is never inserted.
 */
#define HASH_N  4096

/* Into the set, and whether it was new. The probe stops on an empty slot or on the colour. */
static bool seen_put (Uint32 *seen, Uint32 c)
{
	Uint32 k = (c * 2654435761u) & (HASH_N - 1u);
	while (seen[k] && seen[k] != c) k = (k + 1u) & (HASH_N - 1u);
	if (seen[k] == c) return false;

	seen[k] = c;
	return true;
}

/*
 * `keep` is the difference between CREATE and ADD: the list is emptied first, or its colours
 * go into the set first so a pixel that repeats one of them is not listed twice. Either way it
 * is one pass - adding a whole photograph a colour at a time through palette_add would be a
 * search of the list per pixel.
 */
static void scan_into (VNG_PAL *p, const Uint32 *px, int n, bool keep)
{
	Uint32 *seen = (Uint32 *) SDL_calloc(HASH_N, sizeof *seen);
	if (!seen) return;

	if (!keep) p->lot = 0;

	/* A colour kept by hand may be transparent - the colour in hand can be nothing - and zero
	 * is the set's empty slot. Leaving those out of the set costs nothing: the loop below
	 * never asks about a transparent pixel. */
	for (int i = 0; i < p->lot; i++)
		if ((p->c[i] >> 24) != 0u) seen_put(seen, p->c[i]);

	/* Full is asked BEFORE writing, not after: with the list kept, it can be full before the
	 * first pixel, and a test after the write is one colour past the end of the array. */
	for (int i = 0; i < n && p->lot < VNG_PAL_MAX; i++) {
		Uint32 c = px[i];

		/* NOTHING IS NOT A COLOUR. A sprite is mostly hole, and a palette whose first entry
		 * is the hole would spend a cell on it in every drawing. The original skipped it
		 * too. */
		if ((c >> 24) == 0u) continue;

		if (seen_put(seen, c)) p->c[p->lot++] = c;
	}

	SDL_free(seen);
}

/*
 * Built on the first ask and not with the document, which is the same reason the stroke
 * buffers are: a photograph opened to be looked at should not pay for a panel nobody
 * summoned. The original scanned every image on load, whether or not anyone ever looked.
 */
static VNG_PAL *pal_of (VNG_TAB *t)
{
	if (!t) return NULL;
	if (t->pal) return t->pal;

	t->pal = (VNG_PAL *) SDL_calloc(1, sizeof *t->pal);
	if (t->pal) scan_into(t->pal, t->pixels, t->w * t->h, false);
	return t->pal;
}

void palette_free (VNG_PAL *p) { SDL_free(p); }

int palette_lot (VNG_TAB *t)
{
	VNG_PAL *p = pal_of(t);
	return p ? p->lot : 0;
}

Uint32 palette_at (VNG_TAB *t, int i)
{
	VNG_PAL *p = pal_of(t);
	return (p && i >= 0 && i < p->lot) ? p->c[i] : 0u;
}

/* Where a colour sits, or -1. Used to mark the cell holding the colour in hand, which is how
   that marker stays right without anything storing an index: the original kept a static one,
   SHARED BY EVERY DOCUMENT, so switching tabs pointed it at a cell of another palette. */
static int find (VNG_PAL *p, Uint32 argb)
{
	if (!p) return -1;
	for (int i = 0; i < p->lot; i++) if (p->c[i] == argb) return i;
	return -1;
}

bool palette_has (VNG_TAB *t, Uint32 argb) { return find(pal_of(t), argb) >= 0; }

bool palette_add (VNG_TAB *t, Uint32 argb)
{
	VNG_PAL *p = pal_of(t);
	if (!p || p->lot >= VNG_PAL_MAX) return false;
	if (find(p, argb) >= 0) return false;

	p->c[p->lot++] = argb;
	return true;
}

void palette_del (VNG_TAB *t, Uint32 argb)
{
	VNG_PAL *p = pal_of(t);
	int i = find(p, argb);
	if (i < 0) return;

	/* Close the gap, keeping the order. The original walked the WHOLE list looking for the
	 * index and so kept the LAST match rather than the first, then shifted with a loop that
	 * read one past the end of the list it had just shortened. */
	for (int j = i; j < p->lot - 1; j++) p->c[j] = p->c[j + 1];
	p->lot--;
}

void palette_scan (VNG_TAB *t)
{
	VNG_PAL *p = pal_of(t);
	if (p) scan_into(p, t->pixels, t->w * t->h, false);
}

void palette_from (VNG_TAB *t, const Uint32 *px, int n, bool keep)
{
	if (!t || !px || n < 1) return;

	/* A palette about to be REPLACED has no business scanning the whole sheet on its way to
	 * being thrown away, so it skips pal_of. One being ADDED TO is the one the person has been
	 * looking at, which may be the sheet's own - and that has to exist before anything is
	 * added to it. */
	VNG_PAL *p = keep ? pal_of(t) : t->pal;
	if (!p && !keep) p = t->pal = (VNG_PAL *) SDL_calloc(1, sizeof *t->pal);
	if (p) scan_into(p, px, n, keep);
}

/* ------------------------------------------------------- the palettes of other machines */

/*
 * vangopix_palette.ini, one palette per line, and it is the author's own file carried over:
 *
 *     NINTENDO {#000000#fcfcfc#f8f8f8#bcbcbc...}
 *
 * The name up to the first space, then every six hex digits after a hash until the brace.
 * They are RGB and therefore OPAQUE, which is the note at the top of the original's
 * gui_palette.c: only colours with a fixed alpha are stored, so that alpha stays something
 * worked separately - by the slider in the colour window, here.
 *
 * THE PARSER IS NOT THE ORIGINAL'S, and the three reasons are worth keeping:
 *
 *   - __load_palette_list__ wrote into char list[100][100] with NO BOUND on the index, and
 *     then wrote a terminator at list[i] after the loop - so a file of a hundred lines walks
 *     off the end of the array and one of a hundred and one corrupts the stack;
 *   - `while (*++b != 32);` scans for a space with nothing stopping it at the end of the
 *     line, so a line without one reads on into whatever follows in memory;
 *   - the 1KB buffer it allocated was never freed, once per call.
 *
 * The file is found beside the EXECUTABLE and not in the working directory, which is the rule
 * the fonts already follow: a file manager, a shortcut and a terminal each launch from a
 * different place, and only the exe's own directory is the same on all three.
 */
#define LIST_MAX   64
#define NAME_MAX   40
#define LINE_MAX 4096

static char names[LIST_MAX][NAME_MAX];
static int  names_lot  = 0;
static bool names_read = false;

/* Everything up to the first space, and never past the end of the line. */
static void name_of (const char *line, char *dst, size_t cap)
{
	size_t n = 0;
	while (line[n] && line[n] != ' ' && line[n] != '\t' &&
	       line[n] != '\r' && line[n] != '\n' && n + 1 < cap) {
		dst[n] = line[n];
		n++;
	}
	dst[n] = 0;
}

static SDL_IOStream *ini_open (void)
{
	char *path = vangopix_asset("vangopix_palette.ini");
	if (!path) return NULL;

	SDL_IOStream *io = SDL_IOFromFile(path, "r");
	SDL_free(path);
	return io;
}

static void names_load (void)
{
	if (names_read) return;
	names_read = true;
	names_lot  = 0;

	SDL_IOStream *io = ini_open();
	if (!io) return;

	char line[LINE_MAX];
	while (names_lot < LIST_MAX && vangopix_read_line(io, line, sizeof line)) {
		if (!line[0] || line[0] == ' ' || line[0] == '#') continue;
		name_of(line, names[names_lot], NAME_MAX);
		if (names[names_lot][0]) names_lot++;
	}
	SDL_CloseIO(io);
}

/* Two hex digits, or -1. */
static int hex2 (const char *s)
{
	int v = 0;
	for (int i = 0; i < 2; i++) {
		int d;
		if      (s[i] >= '0' && s[i] <= '9') d = s[i] - '0';
		else if (s[i] >= 'a' && s[i] <= 'f') d = s[i] - 'a' + 10;
		else if (s[i] >= 'A' && s[i] <= 'F') d = s[i] - 'A' + 10;
		else return -1;
		v = v * 16 + d;
	}
	return v;
}

/*
 * Replaces the tab's palette with the named one. It REPLACES rather than appends, which is
 * what the original did (img->palette_lot = 0 before the read) and what choosing from a list
 * means: you are saying which set you are working in, not adding to a pile.
 */
static bool named_load (VNG_TAB *t, const char *want)
{
	VNG_PAL *p = pal_of(t);
	if (!p) return false;

	SDL_IOStream *io = ini_open();
	if (!io) return false;

	char line[LINE_MAX], name[NAME_MAX];
	bool got = false;

	while (!got && vangopix_read_line(io, line, sizeof line)) {
		name_of(line, name, sizeof name);
		if (SDL_strcmp(name, want) != 0) continue;

		got    = true;
		p->lot = 0;

		for (const char *s = line; *s && *s != '}'; s++) {
			if (*s != '#') continue;

			int r = hex2(s + 1), g = hex2(s + 3), b = hex2(s + 5);
			if (r < 0 || g < 0 || b < 0) continue;

			/* OPAQUE, always - see the note above. */
			Uint32 c = 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;

			if (find(p, c) < 0 && p->lot < VNG_PAL_MAX) p->c[p->lot++] = c;
			s += 6;
		}
	}
	SDL_CloseIO(io);
	return got;
}

int palette_list_lot (void)
{
	names_load();
	return names_lot;
}

const char *palette_list_name (int i)
{
	names_load();
	return (i >= 0 && i < names_lot) ? names[i] : "";
}

bool palette_load (VNG_TAB *t, const char *name)
{
	if (!t || !name) return false;
	return named_load(t, name);
}

/* --------------------------------------------------------------------------- the tiles */

static int clampi (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static bool in_rect (SDL_FRect r, float x, float y)
{
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* Which cell a point is in. False outside the grid ENTIRELY - which is the test the original
   meant to make and did not: it asked about its first cell only. */
static bool cell_of (SDL_FRect g, int n, float x, float y, int *out)
{
	if (!in_rect(g, x, y)) return false;

	int cx = (int)((x - g.x) / CELL);
	int cy = (int)((y - g.y) / CELL);

	*out = cy * n + cx;
	return true;
}

/*
 * ONE CELL: the colour over the board, then the black frame the original drew at gd+1 so that
 * neighbouring cells SHARE their line rather than doubling it.
 *
 * WHAT IT IS LAID OVER IS NOT THE ORIGINAL'S, AND THAT IS THE ONLY PART THAT CHANGED.
 * __draw_alpha_grid__ drew any non-opaque colour as a left-to-right RAMP from full to nothing,
 * which says a colour has SOME alpha and never which - so every half transparent colour looked
 * identical to every other - and it left the cell's first column unpainted. Here the colour
 * goes on at its real alpha and the board underneath does the telling, which is what the board
 * was there for.
 */
static void cell_draw (SDL_FRect g, int n, int i, Uint32 argb, bool ring)
{
	SDL_FRect r;
	r.x = g.x + (float)(i % n) * CELL;
	r.y = g.y + (float)(i / n) * CELL;
	r.w = CELL;
	r.h = CELL;

	/* The colour straight onto the board, at its REAL alpha. The board is already down under
	 * the whole grid - see grid_draw - so an empty cell is bare checkerboard and a half
	 * transparent one shows exactly how much of it. */
	SDL_SetRenderDrawColor(vng_ren, (Uint8)((argb >> 16) & 0xFF), (Uint8)((argb >> 8) & 0xFF),
	                                (Uint8)(argb & 0xFF), (Uint8)((argb >> 24) & 0xFF));
	SDL_RenderFillRect(vng_ren, &r);

	SDL_FRect line = { r.x, r.y, CELL + 1.0f, CELL + 1.0f };
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xFF);
	SDL_RenderRect(vng_ren, &line);

	if (!ring) return;

	/*
	 * TWO RINGS, WHITE OUTSIDE AND THE COLOUR'S CONTRAST INSIDE - the original's shape, with
	 * the inner one decided by luminance instead of by inverting. Inverting is why a mid-grey
	 * swatch had a marker on it that could not be seen: the inverse of 0x808080 is one step
	 * from itself.
	 */
	SDL_FRect a = { r.x + 1.0f, r.y + 1.0f, CELL - 1.0f, CELL - 1.0f };
	SDL_FRect b = { r.x + 2.0f, r.y + 2.0f, CELL - 3.0f, CELL - 3.0f };

	SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xFF);
	SDL_RenderRect(vng_ren, &a);

	if (tool_light_on(argb)) SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xFF);
	else                     SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xFF);
	SDL_RenderRect(vng_ren, &b);
}

/*
 * THE GRID, AND IT IS DRAWN THE SAME WAY WHEREVER IT IS SUMMONED. Every cell of the shape,
 * filled or not, then the markers.
 *
 * The marked cell is the one holding COLOUR 1, worked out here rather than remembered - so it
 * is right after a colour is absorbed off the sheet with CTRL, after a tab is switched and
 * after a colour is deleted, none of which the original's stored index survived. The cell
 * under the pointer is marked too, which is what says a cell is about to answer.
 */
static void grid_draw (VNG_TAB *t, SDL_FRect g, int n, int m)
{
	VNG_PAL *p    = pal_of(t);
	int      lot  = p ? p->lot : 0;
	int      mark = find(p, tool_colour(0));

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	int hover;
	if (!cell_of(g, n, mx, my, &hover)) hover = -1;

	/*
	 * THE BOARD GOES DOWN FIRST, UNDER THE WHOLE GRID, and it is what says a cell is
	 * transparent - which is the original's __generate_tile__, at its own gd/4.
	 *
	 * One tiled call rather than the original's texture blit per cell. The phase works out
	 * identical because a cell is four whole squares across - two light and two dark, 24px of
	 * 6px squares now, 20px of 5px in the original - so a board drawn straight
	 * through the grid lands exactly where a board restarted at every cell would - and a
	 * seam that only shows up when the numbers stop dividing is a seam worth not having.
	 *
	 * NOT THE DESK'S BOARD, which is the one deliberate exception in the program - see TILE_A
	 * above for why this grid needs tones of its own. Everything else that shows alpha, the
	 * two loaded colours at the bottom of the screen included, uses the desk's.
	 */
	vangopix_board_rect(g, CHECK, TILE_A, TILE_B);

	for (int i = 0; i < n * m; i++)
		cell_draw(g, n, i, i < lot ? p->c[i] : 0u,
		          i < lot && (i == mark || i == hover));
}

/* ------------------------------------------------------------------------ the box, on P */

static VNG_WIN *box  = NULL;
static VNG_WIN *list = NULL;
static bool     btn_armed = false;

static void list_open (void);

/*
 * Inside the window there are two things and no more: the two loaded colours as overlapping
 * discs, and the word that opens the list. That is the whole of gui_palette_box, and it is
 * why the window can be 64 x 64 and stay there.
 *
 * The original filled the discs with RGBA_FF - the colour with its alpha forced opaque - so
 * colour 2, which in this program starts as NOTHING, would have shown as solid black. The
 * desk goes underneath instead and the colour over it at its real alpha, which is how a
 * transparent slot is drawn everywhere else here.
 */
static void box_body (SDL_FRect area, void *ctx)
{
	(void)ctx;

	Uint32 c1 = tool_colour(0), c2 = tool_colour(1);

	vangopix_desk_disc(area.x + C2_X, area.y + C2_Y, RADIUS, c2,
	                   tool_light_on(c2) ? 0xFFFFFFFFu : 0xFF000000u);
	vangopix_desk_disc(area.x + C1_X, area.y + C1_Y, RADIUS, c1,
	                   tool_light_on(c1) ? 0xFFFFFFFFu : 0xFF000000u);

	if (!vng_text) return;

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	SDL_FRect b = { area.x + BTN_X, area.y + BTN_Y, BTN_W, BTN_H };

	/*
	 * THE SMALL FACE, which is what the original drew this with: its font was a 6x6 bitmap and
	 * vango_text_set(0, ...) here against vango_text_set(2, ...) for the swatch count, so the
	 * button was a THIRD the height of everything around it. The 36 x 13 rectangle is sized
	 * for that, and the main face at sixteen does not fit inside it.
	 *
	 * Centred in the rectangle rather than pinned to its corner, so the label sits in the
	 * button whatever the face turns out to measure.
	 */
	float tw, th;
	text_measure(vng_text_small, BTN_TEXT, &tw, &th);

	text_print(vng_text_small,
	           b.x + SDL_floorf((b.w - tw) * 0.5f),
	           b.y + SDL_floorf((b.h - th) * 0.5f),
	           in_rect(b, mx, my) ? 0xFF0000FFu : 0xFFFFFFFFu, BTN_TEXT);
}

static bool box_event (SDL_FRect area, const SDL_Event *e, void *ctx)
{
	(void)ctx;

	float x, y;
	if (e->type == SDL_EVENT_MOUSE_MOTION) { x = e->motion.x; y = e->motion.y; }
	else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
	         e->type == SDL_EVENT_MOUSE_BUTTON_UP)  { x = e->button.x; y = e->button.y; }
	else return false;

	SDL_FRect b = { area.x + BTN_X, area.y + BTN_Y, BTN_W, BTN_H };

	/* It fires on the way UP and over itself, which is the contract the close box and every
	 * other button here keeps - a press that slides off is a press taken back. The original
	 * fired on the press, and its hit rectangle was one pixel above where it drew. */
	if (e->type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (!btn_armed) return false;
		btn_armed = false;
		if (in_rect(b, x, y)) list_open();
		return true;
	}
	if (e->type == SDL_EVENT_MOUSE_MOTION) return btn_armed;

	if (e->button.button == SDL_BUTTON_LEFT && in_rect(b, x, y)) {
		btn_armed = true;
		return true;
	}

	/* Everything else in here is somewhere to take hold of the window - which on a panel this
	 * small is the only handle worth having. */
	return false;
}

/* --------------------------------------------------------------- the grid beside the box */

/* Where it hangs: four pixels off the window's OUTER right edge, its top level with the
   interior - the original's (p->s.x + p->s.w + 4, p->s.y + WINMGR_HEAD). */
SDL_FRect palette_grid_area (void)
{
	SDL_FRect none = { 0.0f, 0.0f, 0.0f, 0.0f };
	if (!win_visible(box)) return none;

	SDL_FRect o = win_outer(box);
	SDL_FRect a = win_area(box);

	SDL_FRect g = { o.x + o.w + GAP, a.y, (float)cols * CELL, (float)rows * CELL };
	return g;
}

/* The three bands the original stretched by: the right edge, the bottom edge, and the corner
   where they meet. Eight pixels each, hanging just outside the last cell. */
static void bands (SDL_FRect g, SDL_FRect *e, SDL_FRect *s, SDL_FRect *c)
{
	e->x = g.x + g.w - 1.0f; e->y = g.y;              e->w = BAND; e->h = g.h;
	s->x = g.x;              s->y = g.y + g.h - 1.0f; s->w = g.w;  s->h = BAND;
	c->x = g.x + g.w - 1.0f; c->y = g.y + g.h - 1.0f; c->w = BAND; c->h = BAND;
}

/* What is being pulled, and which way. */
static bool grip_x = false, grip_y = false;

/*
 * THE POINTER IS THE ONLY THING THAT SAYS THE BANDS ARE THERE. They are eight pixels of
 * nothing lying over the last column and the last row, so without the cursor changing shape
 * there is no way to find out they exist except by dragging and seeing what happens - and a
 * grid nobody knows can be stretched is a grid stuck at nine swatches.
 *
 * The original asked for exactly these three shapes in __tool_palette_stretch__, in the order
 * sideways, up-down, corner - each call overwriting the one before, so the corner survived by
 * being written last. Here the corner is tested FIRST and the chain stops, which says the same
 * thing without depending on the order of three unrelated ifs.
 *
 * It takes a point rather than reading the mouse so the three bands can be checked without a
 * hand on one - the same reason palette_quick_hover does. VNG_CUR_ARROW means "no band here",
 * which is not the same as asking for an arrow: see where this is called.
 */
VNG_CURSOR palette_grid_cursor (float x, float y)
{
	if (!win_visible(box)) return VNG_CUR_ARROW;

	SDL_FRect edge, side, corner;
	bands(palette_grid_area(), &edge, &side, &corner);

	if (in_rect(corner, x, y)) return VNG_CUR_NWSE;
	if (in_rect(edge,   x, y)) return VNG_CUR_WE;
	if (in_rect(side,   x, y)) return VNG_CUR_NS;

	return VNG_CUR_ARROW;
}

/*
 * CTRL BELONGS TO THE PALETTE ONLY WHERE THE PALETTE IS, which is the rule the selection
 * already runs on. Inside the grid, CTRL+left keeps the colour in hand and CTRL+right throws
 * one away - the two things the original's ADD button and __palette_color_delete__ did from
 * inside the colour wheel, which this program refuses to turn into a palette panel.
 *
 * Outside the grid CTRL means nothing here and the event is declined, so the eyedropper on
 * the sheet is untouched.
 */
bool palette_grid_event (const SDL_Event *e, VNG_TAB *t)
{
	if (!win_visible(box) || !t) return false;

	float x, y;
	switch (e->type) {
	case SDL_EVENT_MOUSE_MOTION:      x = e->motion.x; y = e->motion.y; break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:   x = e->button.x; y = e->button.y; break;
	default: return false;
	}

	SDL_FRect g = palette_grid_area();
	SDL_FRect edge, side, corner;
	bands(g, &edge, &side, &corner);

	if (e->type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (!grip_x && !grip_y) return in_rect(g, x, y);
		grip_x = grip_y = false;
		return true;
	}

	if (e->type == SDL_EVENT_MOUSE_MOTION) {
		/* A DRAG THAT IS NOT OURS MUST CROSS FREELY - the same rule the sidebar and the 1:1
		 * panel follow. Consuming motion here would cut a stroke or a pan in half the moment
		 * it passed over the swatches. */
		if (!grip_x && !grip_y) return false;

		/* Measured in CELLS, not pixels - so there is nothing to snap afterwards and the grid
		 * is never momentarily a size it cannot be. The original resized in pixels and then
		 * rounded them down to whole cells on the next frame's draw. */
		if (grip_x) cols = clampi((int)((x - g.x) / CELL + 0.5f), COLS_MIN, COLS_MAX);
		if (grip_y) rows = clampi((int)((y - g.y) / CELL + 0.5f), ROWS_MIN, ROWS_MAX);
		return true;
	}

	/* A press, and the bands answer before the cells: they lie over the last column and the
	 * last row, and a hand at the edge of a grid means to pull it. */
	if (e->button.button == SDL_BUTTON_LEFT) {
		if (in_rect(corner, x, y)) { grip_x = grip_y = true; return true; }
		if (in_rect(edge,   x, y)) { grip_x = true;          return true; }
		if (in_rect(side,   x, y)) { grip_y = true;          return true; }
	}

	int i;
	if (!cell_of(g, cols, x, y, &i)) return false;

	bool ctrl  = (keys_mods() & SDL_KMOD_CTRL) != 0;
	bool right = (e->button.button == SDL_BUTTON_RIGHT);
	int  lot   = palette_lot(t);

	if (ctrl) {
		if (right) { if (i < lot) palette_del(t, palette_at(t, i)); }
		else       palette_add(t, tool_colour(0));
		return true;
	}

	/* THE BUTTON THAT TAKES A COLOUR IS THE BUTTON THAT LAYS IT DOWN, which is the original's
	 * own arrangement here and the rule the whole program runs on. */
	if (i < lot) tool_set_colour(right ? 1 : 0, palette_at(t, i));

	return true;
}

void palette_grid_draw (VNG_TAB *t)
{
	if (!win_visible(box) || !t) return;

	SDL_FRect g = palette_grid_area();

	grid_draw(t, g, cols, rows);

	/*
	 * While a pull is happening the shape follows the PULL and not the pointer, or it would
	 * flicker back to an arrow the moment the hand outran the edge it is dragging.
	 *
	 * ASKING FOR THE ARROW AND NOT ASKING AT ALL ARE DIFFERENT THINGS: over the sheet the tool
	 * has already asked for a crosshair, and a panel that answered "arrow" for every point it
	 * does not care about would take it away.
	 */
	{
		float mx, my;
		SDL_GetMouseState(&mx, &my);

		if      (grip_x && grip_y) tool_cursor(VNG_CUR_NWSE);
		else if (grip_x)           tool_cursor(VNG_CUR_WE);
		else if (grip_y)           tool_cursor(VNG_CUR_NS);
		else {
			VNG_CURSOR c = palette_grid_cursor(mx, my);
			if (c != VNG_CUR_ARROW) tool_cursor(c);
		}
	}

	if (!vng_text) return;

	/*
	 * The count, sixteen pixels above the grid and four in - the original's own place. It says
	 * the WHOLE total, which is the point: it is the only thing that tells you there is more
	 * than the grid is showing, and the bands are how you see it.
	 */
	int lot = palette_lot(t);
	int fit = cols * rows;

	if (lot > fit)
		text_print(vng_text, g.x + 4.0f, g.y - COUNT_H, style_rgba(vng_style.accent),
		           "%d colors  (%d shown)", lot, fit);
	else
		text_print(vng_text, g.x + 4.0f, g.y - COUNT_H, style_rgba(vng_style.accent),
		           "%d colors", lot);
}

/* -------------------------------------------------------------------- the list, on COLOR */

static int list_top = 0;   /* the first row showing - what winmgr_scrollbar worked out */

/*
 * THE FIRST ROW IS THE DOCUMENT ITSELF, and it is the original's own abandoned idea: there is
 * a commented-out strcpy(list[i++], "[SELECT COLOR]") at the top of __load_palette_list__.
 *
 * It earns the row. Every other entry REPLACES the palette with a machine's fixed set, and
 * without this there would be no way back to the colours that are actually in the drawing -
 * the original scanned those once on load and gave you no way to ask again.
 */
#define SELF "[THIS IMAGE]"

static int rows_in_list (void) { return palette_list_lot() + 1; }

static const char *row_name (int i)
{
	return i <= 0 ? SELF : palette_list_name(i - 1);
}

static void list_body (SDL_FRect area, void *ctx)
{
	(void)ctx;

	prim_fill(area, vng_style.win_inset);

	if (!vng_text) return;

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	int fit = (int)(area.h / ROW);
	int lot = rows_in_list();

	if (list_top > lot - fit) list_top = lot - fit;
	if (list_top < 0)         list_top = 0;

	for (int j = 0; j < fit && list_top + j < lot; j++) {
		int   i = list_top + j;
		float y = area.y + (float)j * ROW;

		SDL_FRect row = { area.x, y, area.w, ROW };
		bool hot = in_rect(row, mx, my);

		/* Hovered: framed in white with white text; otherwise orange. The original's own two
		 * states, and the frame is what says a row is a thing that can be pressed. */
		if (hot) {
			SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xFF);
			SDL_RenderRect(vng_ren, &row);
		}

		char cut[NAME_MAX];
		text_fit(vng_text, cut, sizeof cut, row_name(i), area.w - 12.0f);

		text_print(vng_text, area.x + 5.0f, y + 2.0f,
		           hot ? 0xFFFFFFFFu : style_rgba(vng_style.accent), "%s", cut);
	}

	/* A mark saying there is more below rather than a scrollbar: winmgr_scrollbar was a whole
	 * widget, with its own sub-area machinery, for a list of ten. The wheel scrolls. */
	if (list_top + fit < lot)
		text_print(vng_text, area.x + area.w - 12.0f, area.y + area.h - ROW,
		           0x808080FFu, "v");
}

static bool list_event (SDL_FRect area, const SDL_Event *e, void *ctx)
{
	(void)ctx;

	VNG_TAB *t = vng_tab;

	if (e->type == SDL_EVENT_MOUSE_WHEEL) {
		list_top -= (int)e->wheel.integer_y;
		return true;
	}
	if (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN) return false;
	if (e->button.button != SDL_BUTTON_LEFT)    return false;
	if (!t) return false;

	int j = (int)((e->button.y - area.y) / ROW);
	int i = list_top + j;

	if (e->button.y < area.y || i < 0 || i >= rows_in_list()) return false;

	if (i == 0) palette_scan(t);
	else        palette_load(t, row_name(i));

	return true;
}

static void list_open (void)
{
	if (list) {
		win_show(list, true);
		return;
	}

	SDL_FRect  a = { LIST_X, LIST_Y, LIST_W, LIST_H };
	SDL_FPoint m = { 100.0f, ROW * 3.0f };

	list = win_open("palettes", a, m, list_body, list_event, NULL);
}

bool palette_list_visible (void) { return win_visible(list); }

/*
 * IT COMES UP UNDER THE POINTER, centred on it - the first Vangopix's behaviour for every
 * summoned window, and the same reason as the 1:1 panel and the colour wheel.
 */
void palette_toggle (void) {

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	if (box) {
		bool on = !win_visible(box);
		if (on) win_place(box, mx, my);
		else if (list) win_show(list, false);   /* the list belongs to the box */
		win_show(box, on);
		return;
	}

	SDL_FRect  a = {0.0f, 0.0f, BOX_W, BOX_H};
	SDL_FPoint m = {BOX_W, BOX_H};

	box = win_open("palette", a, m, box_body, box_event, NULL);
	win_fixed(box, true);
	win_place(box, mx, my);
}

bool palette_visible (void) {return win_visible(box);}

/* ------------------------------------------------------------------- the grid under ALT */

static bool       quick_on = false;
static SDL_FPoint anchor   = {0.0f, 0.0f};
static Uint32     keep     = 0u;

/*
 * POLLED THROUGH keys_mods AND NOT READ OFF THE EVENT, which is the contract keys.h states:
 * while a field owns the keyboard nothing is held, so ALT typed into the colour window's hex
 * box does not summon a palette over the top of it. Polling is also what survives a lost
 * key-up - alt-tabbing away with the key down cannot strand the grid on screen.
 *
 * The EDGE is what matters: on the way down the grid takes its place from the hand and
 * remembers the colour that was in it, so the whole gesture can be given back.
 */
static void quick_check (void) {

	bool on = (keys_mods() & SDL_KMOD_ALT) != 0;
	if (on == quick_on) return;

	quick_on = on;
	if (!on) return;

	SDL_GetMouseState(&anchor.x, &anchor.y);
	keep = tool_colour(0);
}

/*
 * Where it hangs: centred on the frozen pointer and eight pixels below it, which is the
 * original's {mouse_p.x - (g.w / 2), mouse_p.y + 8}.
 *
 * Clamped into the window, which the original did not do - a grid summoned near an edge went
 * off it, and the one thing it exists to show could not be reached. The readout's half height
 * is part of what has to fit, or the grid clears the bottom and its own answer does not.
 */
SDL_FRect palette_quick_area (void) {

	/* EVERY WAY IN SYNCS FIRST. The edge is what sets the anchor, so whichever of these four
	 * calls a frame happens to reach first has to be the one that catches it - otherwise the
	 * answer depends on the order they were called in, which is a trap rather than a rule. */
	quick_check();

	SDL_FRect none = { 0.0f, 0.0f, 0.0f, 0.0f };
	if (!quick_on) return none;

	float gw = (float)cols * CELL, gh = (float)rows * CELL;
	float bw, bh;
	tool_bar_size(&bw, &bh);

	SDL_FRect g = { anchor.x - gw * 0.5f, anchor.y + DROP, gw, gh };

	float need = gh + READ_MID + bh * 0.5f;

	if (g.x + gw   > (float)vng_win_w) g.x = (float)vng_win_w - gw;
	if (g.y + need > (float)vng_win_h) g.y = (float)vng_win_h - need;
	if (g.x < 0.0f) g.x = 0.0f;
	if (g.y < 0.0f) g.y = 0.0f;

	return g;
}

bool palette_quick_event (const SDL_Event *e, VNG_TAB *t) {

	quick_check();
	if (!quick_on || !t) return false;

	float x, y;

	switch (e->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		x = e->button.x;
		y = e->button.y;
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		x = e->wheel.mouse_x;
		y = e->wheel.mouse_y;
		break;

	/* MOTION IS DELIBERATELY NOT CONSUMED, the same rule the sidebar and the 1:1 panel
	 * follow: a drag begun on the sheet that crosses this - a stroke, a pan, a grip pulled
	 * across - must not be cut in half by something that appeared under the hand. Nothing is
	 * dragged in here anyway; the hover is read in the draw. */
	default:
		return false;
	}

	int i;
	if (!cell_of(palette_quick_area(), cols, x, y, &i)) return false;

	if (e->type != SDL_EVENT_MOUSE_BUTTON_DOWN) return true;
	if (i >= palette_lot(t)) return true;

	Uint32 c = palette_at(t, i);

	if (e->button.button == SDL_BUTTON_RIGHT) {
		tool_set_colour(1, c);
	} else {
		/* A PRESS IS A DECISION AND A HOVER IS A PROPOSAL: pressing makes this the colour to
		 * go back to, so sliding off the grid afterwards no longer takes it away. */
		tool_set_colour(0, c);
		keep = c;
	}
	return true;
}

/*
 * WHAT THE GRID ANSWERS FOR A POINT: the colour under it, or - anywhere off the grid - the one
 * that was in hand when the key went down.
 *
 * It is a function of a point rather than of the mouse so that the thing the original got
 * wrong can be checked without a hand on the mouse. gui_quickly_palette_box tested
 * select_rect(pos.x, pos.y, gd, gd) for this, which is its FIRST CELL - so the gesture could
 * only be given back by leaving through the top left corner, and from anywhere else it kept
 * whatever had last been swept over.
 */
Uint32 palette_quick_hover (VNG_TAB *t, float x, float y) {

	quick_check();
	if (!quick_on || !t) return tool_colour(0);

	int i;
	if (!cell_of(palette_quick_area(), cols, x, y, &i)) return keep;
	if (i >= palette_lot(t)) return keep;

	return palette_at(t, i);
}

void palette_quick_draw (VNG_TAB *t) {

	quick_check();
	if (!quick_on || !t) return;

	SDL_FRect g = palette_quick_area();

	float mx, my;
	SDL_GetMouseState(&mx, &my);

	/*
	 * THE HOVER IS THE PICK. It is done here rather than in the event handler because a hover
	 * has no event of its own to hang on - the hand can come to rest on a cell and send
	 * nothing further - which is why the original picked from inside its draw as well.
	 */
	tool_set_colour(0, palette_quick_hover(t, mx, my));

	grid_draw(t, g, cols, rows);

	/*
	 * The hex, centred sixteen pixels under the grid - the original's own place for it. It is
	 * the program's swatch bar rather than bare text, so the value is legible over any colour
	 * and a transparent one still looks transparent; the original printed it in the INVERSE of
	 * the colour, which disappears exactly at mid grey.
	 */
	float bw, bh;
	tool_bar_size(&bw, &bh);

	SDL_FRect bar = { g.x + g.w * 0.5f - bw * 0.5f,
	                  g.y + g.h + READ_MID - bh * 0.5f, bw, bh };

	tool_bar_draw(bar, tool_colour(0));
}