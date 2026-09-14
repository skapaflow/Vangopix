#include "tabs.h"
#include "undo.h"
#include "select.h"
#include "palette.h"
#include "anim.h"
#include "tool.h"

VNG_TAB *vng_tabs = NULL;
VNG_TAB *vng_tab  = NULL;

static VNG_TAB *tail = NULL;

/*
 * The "untitled" counter only ever goes up. This fixes a bug in the previous Vangopix,
 * where one field served both as the name counter and as the tab count, and so went
 * down on close. Open three, close the first, open another, and two live tabs were
 * both named "untitled 2"; since the tab/image join was a hash of the name, the new
 * tab started showing the old one's image. Here the name identifies nothing (the
 * pointer does), but two documents sharing a name still confuse the person looking at
 * them - so the counter stays monotonic.
 */
static unsigned untitled_seq = 1;

/* Never reused, never wrapped in practice: one per document opened, so a person would
 * have to open four billion of them in one session to see it come round. */
static Uint32 id_seq = 1;

/* The name shown in the title: the file only, no directories.
 *
 * THE EXTENSION STAYS. The previous Vangopix stripped it (cutpath then cutpoint),
 * which makes sense in an editor that only opens png; in a general viewer the
 * extension is half the information - knowing a file is .webp and not .png changes
 * what you are able to save.
 *
 * CUT ON A CHARACTER, NOT ON A BYTE. A long name is shortened to fit the field, and a
 * plain strlcpy can stop halfway through an accented letter - which the title bar then
 * shows as garbage. SDL_utf8strlcpy stops before the character it cannot finish. */
static void tab_set_name (VNG_TAB *t, const char *path)
{
	const char *base = path;
	for (const char *p = path; *p; p++)
		if (*p == '/' || *p == '\\')
			base = p + 1;
	SDL_utf8strlcpy(t->name, base, sizeof t->name);
}

static void tab_link (VNG_TAB *t)
{
	t->prev = tail;
	t->next = NULL;
	if (tail) tail->next = t;
	else      vng_tabs = t;
	tail = t;
}

/* ------------------------------------------------------------- what the GPU has seen */

/* Widens a half-open rectangle held as four corners to take in another. An empty one - x1 not
   past x0 - simply becomes the other. */
static void widen (int *x0, int *y0, int *x1, int *y1, int ax, int ay, int bx, int by)
{
	if (*x1 <= *x0 || *y1 <= *y0) {
		*x0 = ax; *y0 = ay; *x1 = bx; *y1 = by;
		return;
	}
	if (ax < *x0) *x0 = ax;
	if (ay < *y0) *y0 = ay;
	if (bx > *x1) *x1 = bx;
	if (by > *y1) *y1 = by;
}

void vng_tab_touch (VNG_TAB *t, int x, int y, int w, int h)
{
	if (!t || w <= 0 || h <= 0) return;

	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
	int x1 = x + w > t->w ? t->w : x + w;
	int y1 = y + h > t->h ? t->h : y + h;
	if (x1 <= x0 || y1 <= y0) return;

	widen(&t->tx0, &t->ty0, &t->tx1, &t->ty1, x0, y0, x1, y1);
}

static void touch_all (VNG_TAB *t) { vng_tab_touch(t, 0, 0, t->w, t->h); }

/* The same bookkeeping for the preview texture, which nothing outside this file writes. */
static void preview_touch (VNG_TAB *t, int x0, int y0, int x1, int y1)
{
	if (x1 <= x0 || y1 <= y0) return;
	widen(&t->px0, &t->py0, &t->px1, &t->py1, x0, y0, x1, y1);
}

/*
 * Sends what changed and nothing else, from the buffer that owns it straight into the
 * rectangle it came from - the pitch is the whole sheet's, so a sub-rectangle needs no copy
 * of its own on the way.
 *
 * THE PREVIEW IS SENT WHETHER OR NOT A STROKE IS OPEN. A closed stroke leaves its rectangle
 * transparent again in the buffer, and the texture has to hear that before the next stroke
 * is composited over the sheet - or the last one's pixels would come back with it.
 */
void vng_tab_upload (VNG_TAB *t)
{
	if (!t) return;

	if (t->tex && t->pixels && t->tx1 > t->tx0 && t->ty1 > t->ty0) {
		SDL_Rect r = { t->tx0, t->ty0, t->tx1 - t->tx0, t->ty1 - t->ty0 };
		SDL_UpdateTexture(t->tex, &r, t->pixels + (size_t)r.y * t->w + r.x,
		                  t->w * (int)sizeof(Uint32));
	}
	t->tx0 = t->ty0 = t->tx1 = t->ty1 = 0;

	if (t->tex_preview && t->pixels_preview && t->px1 > t->px0 && t->py1 > t->py0) {
		SDL_Rect r = { t->px0, t->py0, t->px1 - t->px0, t->py1 - t->py0 };
		SDL_UpdateTexture(t->tex_preview, &r, t->pixels_preview + (size_t)r.y * t->w + r.x,
		                  t->w * (int)sizeof(Uint32));
	}
	t->px0 = t->py0 = t->px1 = t->py1 = 0;
}

int vng_tab_side_limit (void)
{
	static int limit = 0;

	if (limit > 0) return limit;
	if (!vng_ren)  return VNG_MAX_SIDE;   /* not asked yet, and not remembered either */

	Sint64 n = SDL_GetNumberProperty(SDL_GetRendererProperties(vng_ren),
	                                 SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
	limit = (n > 0 && n <= SDL_MAX_SINT32) ? (int)n : VNG_MAX_SIDE;
	return limit;
}

/* A texture has a fixed size from creation, and opening a file changes the document's
 * dimensions - which is why it is born here and not where the sheet is filled in. */
static SDL_Texture *texture_make (int w, int h)
{
	SDL_Texture *tex = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
	                                     SDL_TEXTUREACCESS_STREAMING, w, h);
	if (!tex) {
		SDL_Log("SDL_CreateTexture: %s", SDL_GetError());
		return NULL;
	}
	/* Without this the alpha in the buffer is carried to the GPU and then ignored: the
	 * default for a new texture is no blending, so a half transparent pixel would draw
	 * as fully opaque and the checkerboard behind it would never show. */
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	return tex;
}

static bool tab_make_texture (VNG_TAB *t)
{
	SDL_Texture *tex = texture_make(t->w, t->h);
	if (!tex) return false;

	if (t->tex) SDL_DestroyTexture(t->tex);
	t->tex = tex;
	touch_all(t);
	return true;
}

/* Thrown away rather than resized: a resize cannot happen in the middle of a stroke, so
 * there is never anything in them worth carrying to the new geometry, and the next
 * stroke allocates them at the right size for nothing. */
static void draw_buffers_free (VNG_TAB *t)
{
	if (t->tex_preview) SDL_DestroyTexture(t->tex_preview);
	t->tex_preview = NULL;

	SDL_free(t->pixels_preview);
	SDL_free(t->mask);
	t->pixels_preview = NULL;
	t->mask           = NULL;
	t->stroke         = false;
	t->px0 = t->py0 = t->px1 = t->py1 = 0;
}

static bool draw_buffers_make (VNG_TAB *t)
{
	if (t->pixels_preview && t->mask && t->tex_preview) return true;
	draw_buffers_free(t);

	/* calloc, and it matters: the preview must start fully transparent, because it is
	 * composited over the document every frame a stroke is open. */
	t->pixels_preview = (Uint32 *) SDL_calloc((size_t)t->w * t->h, sizeof(Uint32));
	t->mask           = (Uint8  *) SDL_calloc((size_t)t->w * t->h, 1);

	t->tex_preview = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
	                                   SDL_TEXTUREACCESS_STREAMING, t->w, t->h);

	if (!t->pixels_preview || !t->mask || !t->tex_preview) {
		SDL_Log("stroke buffers: %s", SDL_GetError());
		draw_buffers_free(t);
		return false;
	}

	SDL_SetTextureBlendMode(t->tex_preview, SDL_BLENDMODE_BLEND);

	/* A new texture holds whatever was in that memory. It is cleared once, here, and
	 * from then on every stroke leaves it transparent again on the way out - which is
	 * what lets the per-frame upload be the touched rectangle and not the whole sheet. */
	SDL_UpdateTexture(t->tex_preview, NULL, t->pixels_preview,
	                  t->w * (int)sizeof(Uint32));
	return true;
}

/* Throws an open stroke away whole: rewound if it wrote through, wiped if it was a preview,
 * and its undo step - empty by then - discarded on the way out. */
static void stroke_cancel (VNG_TAB *t)
{
	vng_tab_stroke_reset(t);
	vng_tab_stroke_close(t);
}

static bool stroke_begin (VNG_TAB *t, bool direct, bool clipped)
{
	if (!t) return false;

	/*
	 * ONE STROKE AT A TIME, AND THE FIRST IS CANCELLED RATHER THAN ORPHANED.
	 *
	 * This used to carry straight on: undo_open threw the first stroke's step away and the
	 * rectangle below was reset, so a preview still sitting in the buffers was forgotten
	 * with its mask set - and merged into the document by the next stroke whose rectangle
	 * happened to cover it. A safety net and not the protocol: whoever changes the sheet
	 * calls vng_tab_settle first, and then this never fires. It says so if it does.
	 */
	if (t->stroke) {
		SDL_Log("stroke: opened over one still open - the first was cancelled");
		stroke_cancel(t);
	}

	if (!draw_buffers_make(t)) return false;
	if (!undo_open(t)) return false;

	t->direct = direct;

	/* THE EDGE IS DECIDED HERE, ONCE, like the colour a tool lays: a selection changed while a
	 * stroke is running must not move the edge of a line already begun. select_area leaves the
	 * whole sheet in place when nothing is marked, and hands back a rectangle already clamped
	 * to the sheet - possibly empty, when what is marked covers no pixel of it, and then
	 * nothing is writable, because nothing is inside the selection. */
	SDL_Rect r = { 0, 0, t->w, t->h };
	if (clipped) select_area(t, &r);
	t->clip = r;

	/* An empty box, stated so that the first put widens it in both directions. */
	t->sx0 = t->w; t->sy0 = t->h;
	t->sx1 = 0;    t->sy1 = 0;
	t->stroke = true;
	return true;
}

bool vng_tab_stroke_open (VNG_TAB *t, bool direct)
{
	return stroke_begin(t, direct, true);
}

/* select.c's put_down, and nobody else's - see tabs.h. */
bool vng_tab_stroke_open_unclipped (VNG_TAB *t, bool direct)
{
	return stroke_begin(t, direct, false);
}

/* The clip is clamped to the sheet when it is taken and the geometry cannot change while a
 * stroke is open (adopt throws the stroke away), so inside the clip is inside the buffer. */
static bool in_clip (const VNG_TAB *t, int x, int y)
{
	return x >= t->clip.x && y >= t->clip.y &&
	       x <  t->clip.x + t->clip.w && y < t->clip.y + t->clip.h;
}

bool vng_tab_writable (VNG_TAB *t, int x, int y)
{
	return t && t->stroke && in_clip(t, x, y);
}

bool vng_tab_touched (VNG_TAB *t, int x, int y)
{
	if (!t || !t->mask || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
	return t->mask[(size_t)y * t->w + x] != 0;
}

void vng_tab_put (VNG_TAB *t, int x, int y, Uint32 argb)
{
	/* THE SELECTION HOLDS HERE, for every tool at once. Refused and not clamped: a pixel
	 * outside the edge is simply not part of this stroke - not written, not carried, not
	 * marked - so a stroke that never crosses into the selection leaves no undo step. */
	if (!t || !t->stroke || !in_clip(t, x, y)) return;

	size_t i = (size_t)y * t->w + x;

	if (t->direct) {
		/* Straight into the document, carry first. There is no preview to composite and
		 * nothing to merge later - which is the whole point, since what this stroke lays
		 * cannot be shown by drawing it over anything.
		 *
		 * A pixel that already IS this colour is marked and not carried: nothing changed,
		 * so there is nothing for undo to put back - see tabs.h. */
		if (t->pixels[i] != argb) {
			undo_carry(t, x, y, t->pixels[i], argb);
			t->pixels[i] = argb;
			vng_tab_touch(t, x, y, 1, 1);
		}
	} else {
		t->pixels_preview[i] = argb;
		preview_touch(t, x, y, x + 1, y + 1);
	}

	t->mask[i] = 1;   /* the mask does its job either way: one write per pixel per stroke */

	if (x     < t->sx0) t->sx0 = x;
	if (y     < t->sy0) t->sy0 = y;
	if (x + 1 > t->sx1) t->sx1 = x + 1;
	if (y + 1 > t->sy1) t->sy1 = y + 1;
}

/* Clears the touched rectangle in both buffers, and tells the preview texture so - it is
 * re-sent on the next upload, which is what keeps it transparent everywhere again: the
 * invariant the partial upload during a stroke depends on. */
static void preview_wipe (VNG_TAB *t)
{
	if (t->sx1 <= t->sx0 || t->sy1 <= t->sy0) return;

	for (int y = t->sy0; y < t->sy1; y++) {
		size_t row = (size_t)y * t->w;
		SDL_memset(t->pixels_preview + row + t->sx0, 0,
		           (size_t)(t->sx1 - t->sx0) * sizeof(Uint32));
		SDL_memset(t->mask + row + t->sx0, 0, (size_t)(t->sx1 - t->sx0));
	}

	preview_touch(t, t->sx0, t->sy0, t->sx1, t->sy1);

	t->sx0 = t->w; t->sy0 = t->h;
	t->sx1 = 0;    t->sy1 = 0;
}

/* Empties the touched rectangle in the mask without touching the pixels - what a
 * write-through stroke needs, since its pixels are put back by the undo step instead. */
static void mask_wipe (VNG_TAB *t)
{
	if (t->sx1 <= t->sx0 || t->sy1 <= t->sy0) return;

	for (int y = t->sy0; y < t->sy1; y++)
		SDL_memset(t->mask + (size_t)y * t->w + t->sx0, 0, (size_t)(t->sx1 - t->sx0));

	t->sx0 = t->w; t->sy0 = t->h;
	t->sx1 = 0;    t->sy1 = 0;
}

void vng_tab_stroke_reset (VNG_TAB *t)
{
	if (!t || !t->stroke) return;

	if (t->direct) {
		/* The step itself is the record of what to put back, so rewinding it IS the reset.
		 * That is what lets a shape be dragged in a colour that removes rather than adds. */
		undo_rewind(t);
		mask_wipe(t);
	} else {
		preview_wipe(t);
	}
}

void vng_tab_stroke_close (VNG_TAB *t)
{
	if (!t || !t->stroke) return;
	t->stroke = false;

	/* Nothing to merge: every pixel went in as it was drawn, and its carry with it. */
	if (t->direct) {
		mask_wipe(t);
		undo_close(t);
		return;
	}

	/* Only the rectangle the stroke actually reached is walked. The alternative is the
	 * whole sheet on every stroke, which on a large canvas is millions of untouched
	 * pixels read to find out they are untouched. */
	for (int y = t->sy0; y < t->sy1; y++) {
		for (int x = t->sx0; x < t->sx1; x++) {
			size_t i = (size_t)y * t->w + x;
			if (!t->mask[i]) continue;

			/* A pixel laid in the colour it already had is not a change, and is not
			 * carried - see tabs.h. */
			if (t->pixels[i] != t->pixels_preview[i]) {
				undo_carry(t, x, y, t->pixels[i], t->pixels_preview[i]);
				t->pixels[i] = t->pixels_preview[i];
			}

			/* Left clean for the next stroke, so opening one costs nothing. */
			t->pixels_preview[i] = 0;
			t->mask[i]           = 0;
		}
	}

	/* The same rectangle, now transparent in the preview and merged into the sheet, is owed
	 * to both textures. */
	preview_touch(t, t->sx0, t->sy0, t->sx1, t->sy1);
	if (t->sx1 > t->sx0 && t->sy1 > t->sy0)
		vng_tab_touch(t, t->sx0, t->sy0, t->sx1 - t->sx0, t->sy1 - t->sy0);

	undo_close(t);
}

/* ------------------------------------------------------------------------ the documents */

static VNG_TAB *tab_alloc (int w, int h)
{
	VNG_TAB *t = (VNG_TAB *) SDL_calloc(1, sizeof *t);
	if (!t) return NULL;

	t->id = id_seq++;
	t->w = w;
	t->h = h;
	t->pixels = (Uint32 *) SDL_malloc((size_t)w * h * sizeof(Uint32));
	if (!t->pixels) { SDL_free(t); return NULL; }

	/* Zoom 0 means "never framed". view_sheet_rect frames it on the first draw, when
	 * the window size is certainly known. Setting 1.0 here would show every opened
	 * image at 1:1 and let a 4000px photograph arrive off screen. */
	t->zoom = 0.0f;
	return t;
}

/* Everything a tab owns, and the tab. One place, because closing one tab and closing all of
 * them at the end are the same job - and two copies of a list of frees are a leak waiting for
 * the next thing a tab grows. */
static void tab_destroy (VNG_TAB *t)
{
	if (t->tex) SDL_DestroyTexture(t->tex);
	draw_buffers_free(t);
	undo_free(t->undo);
	select_free(t->sel);
	palette_free(t->pal);
	anim_tab_free(t->anim);
	SDL_free(t->pixels);
	SDL_free(t->path);
	SDL_free(t);
}

VNG_TAB *vng_tab_new (int w, int h)
{
	VNG_TAB *t = tab_alloc(w, h);
	if (!t) return NULL;

	for (size_t i = 0, n = (size_t)w * h; i < n; i++)
		t->pixels[i] = 0xFFFFFFFFu;   /* the sheet is born white and opaque */

	SDL_snprintf(t->name, sizeof t->name, "untitled %u", untitled_seq++);

	if (!tab_make_texture(t)) { SDL_free(t->pixels); SDL_free(t); return NULL; }

	tab_link(t);
	vng_tab_show(t);
	return t;
}

/*
 * AN OPEN THAT FAILS SAYS SO, IN A BOX.
 *
 * Every way into this call is a person asking for exactly this file - a drop, CTRL+O, a row
 * of the project panel, the command line - and the program is built without a console, so the
 * log that used to be the whole answer went to nobody. A drop that did nothing is read as the
 * drop not having worked, and then as the program being broken.
 */
static void refuse (const char *path, const char *why)
{
	const char *base = path;
	for (const char *p = path; *p; p++)
		if (*p == '/' || *p == '\\') base = p + 1;

	char msg[512];
	SDL_snprintf(msg, sizeof msg, "Could not open %s.\n\n%s", base, why ? why : "");

	SDL_Log("open %s: %s", path, why ? why : "");
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, VNG_NAME, msg, vng_win);
}

/*
 * Opens anything SDL3_image can read and puts it in a new tab.
 *
 * Converting to ARGB8888 is mandatory, and it is not a detail: the file may arrive
 * 8-bit indexed, grayscale, or 16 bits per channel, and the sheet has exactly one
 * format. An editor with a polymorphic buffer is an editor with one bug per format.
 * ARGB because that is what an SDL texture takes with no conversion on the way in.
 *
 * VERIFIED: an indexed png arrives here as SDL_PIXELFORMAT_INDEX8 with its SDL_Palette
 * intact - SDL3_image does not flatten it on the way in. Nothing reads that palette
 * today, but the day indexed colour mode exists, this is where it would be captured,
 * before the conversion below throws it away. (stb_image would have expanded to RGBA
 * and the indices would already be gone - part of why the satellite is worth its dll.)
 */
VNG_TAB *vng_tab_open (const char *path)
{
	if (!path || !path[0]) return NULL;

	/* A FILE ALREADY OPEN IS SHOWN, NOT OPENED AGAIN - see tabs.h. */
	for (VNG_TAB *p = vng_tabs; p; p = p->next)
		if (p->path && vangopix_path_same(p->path, path)) {
			vng_tab_show(p);
			return p;
		}

	SDL_Surface *raw = IMG_Load(path);
	if (!raw) {
		refuse(path, SDL_GetError());
		return NULL;
	}

	/* Asked BEFORE the conversion and the copy: a picture the GPU cannot hold as one texture
	 * cannot be a sheet, and finding that out after two full-size copies of it is finding it
	 * out slowly and with the machine's memory. */
	int limit = vng_tab_side_limit();
	if (raw->w > limit || raw->h > limit) {
		char why[160];
		SDL_snprintf(why, sizeof why,
		             "It is %d x %d, and this machine can show at most %d pixels a side.",
		             raw->w, raw->h, limit);
		SDL_DestroySurface(raw);
		refuse(path, why);
		return NULL;
	}

	SDL_Surface *img = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
	SDL_DestroySurface(raw);
	if (!img) {
		refuse(path, SDL_GetError());
		return NULL;
	}

	VNG_TAB *t = tab_alloc(img->w, img->h);
	if (!t) {
		SDL_DestroySurface(img);
		refuse(path, "There is not enough memory for it.");
		return NULL;
	}

	/* Row by row: a surface's pitch may carry padding at the end of each line, and a
	 * single memcpy would drag that garbage into the document. */
	for (int y = 0; y < t->h; y++)
		SDL_memcpy(t->pixels + (size_t)y * t->w,
		           (Uint8 *)img->pixels + (size_t)y * img->pitch,
		           (size_t)t->w * sizeof(Uint32));
	SDL_DestroySurface(img);

	t->path = SDL_strdup(path);
	tab_set_name(t, path);

	if (!t->path || !tab_make_texture(t)) {
		SDL_free(t->path); SDL_free(t->pixels); SDL_free(t);
		refuse(path, SDL_GetError());
		return NULL;
	}

	tab_link(t);
	vng_tab_show(t);
	return t;
}

/*
 * CLOSING THE LAST SHEET LEAVES NO SHEET, and the empty desk is a real state now.
 *
 * It used to open a blank one in its place, and the reason given was sound: an editor whose
 * whole interface is the sheet has nothing to click and nothing explaining what to do without
 * one. That was an argument about the EMPTY SCREEN, not about the document - and the answer
 * to an empty screen is to put something on it. core.c now writes the keys there, so the
 * state that had to be avoided is the state that teaches the program.
 *
 * What it buys: a blank untitled sheet nobody asked for is not sitting in the way of the file
 * somebody is about to drop in, and CTRL+W means what it says.
 *
 * THE SHEET ON SCREEN STAYS ON SCREEN unless it is the one going. The fallback used to be
 * taken whichever tab was closed, so the [x] on a tab BEHIND the current one - which is what
 * that [x] is for - pulled the eye off the drawing onto the closed tab's neighbour.
 */
void vng_tab_close (VNG_TAB *t)
{
	if (!t) return;

	VNG_TAB *fallback  = t->next ? t->next : t->prev;
	bool     on_screen = (t == vng_tab);

	if (t->prev) t->prev->next = t->next; else vng_tabs = t->next;
	if (t->next) t->next->prev = t->prev; else tail     = t->prev;

	tab_destroy(t);

	if (on_screen)
		vng_tab = fallback;   /* NULL when that was the last one - the desk, and the keys on it */
	vng_tab_title();
}

/* dir > 0 walks right, dir < 0 walks left; wraps around at both ends. */
void vng_tab_step (int dir)
{
	if (!vng_tab) return;

	VNG_TAB *n = dir > 0 ? vng_tab->next : vng_tab->prev;
	if (!n) n = dir > 0 ? vng_tabs : tail;
	if (!n || n == vng_tab) return;

	vng_tab_show(n);
}

void vng_tab_settle (VNG_TAB *t)
{
	if (!t) return;

	/* The tool first, so its stroke is closed before the selection opens one of its own to
	 * put a float down - two strokes open at once on one sheet is the thing being ended. */
	tool_settle(t);
	select_settle(t);
}

void vng_tab_show (VNG_TAB *t)
{
	if (!t || t == vng_tab) return;

	/* What was in flight on the outgoing sheet is finished before it stops being drawn. */
	vng_tab_settle(vng_tab);

	vng_tab = t;
	vng_tab_title();
}

/*
 * Pulls t out of the row and puts it back at index (0-based, clamped).
 *
 * The list is the ORDER - there is no separate array of positions to keep in step with
 * it, which is the whole reason reordering is a relink and not a sort. Costly only in
 * pointer writes, and a person can only drag one tab at a time.
 */
void vng_tab_move (VNG_TAB *t, int index)
{
	if (!t) return;

	int n = vng_tab_count();
	if (index < 0)  index = 0;
	if (index >= n) index = n - 1;
	if (index == vng_tab_index(t) - 1) return;   /* vng_tab_index is 1-based */

	/* unlink */
	if (t->prev) t->prev->next = t->next; else vng_tabs = t->next;
	if (t->next) t->next->prev = t->prev; else tail     = t->prev;
	t->prev = t->next = NULL;

	/* relink before the tab currently sitting at index */
	VNG_TAB *at = vng_tabs;
	for (int i = 0; i < index && at; i++) at = at->next;

	if (!at) {                       /* dropped past the end */
		t->prev = tail;
		if (tail) tail->next = t; else vng_tabs = t;
		tail = t;
	} else {
		t->next = at;
		t->prev = at->prev;
		if (at->prev) at->prev->next = t; else vng_tabs = t;
		at->prev = t;
	}
}

/*
 * Resizing is a NEW buffer plus a copy, never a realloc.
 *
 * A realloc keeps the bytes in order and the bytes are the wrong shape: the pixel at
 * (x, y) sits at y * w + x, so changing w moves every row but the first. The copy walks
 * rows precisely because the stride changes underneath it.
 *
 * The new buffer is filled white first, so any area the old image does not cover comes
 * out as paper rather than as whatever the allocator had lying there.
 */
Uint32 *vng_tab_adopt (VNG_TAB *t, Uint32 *pixels, int w, int h)
{
	if (!t || !pixels || w < 1 || h < 1) return NULL;

	/* The texture is made BEFORE anything is swapped, so a failure here leaves the
	 * document exactly as it was rather than half changed. */
	SDL_Texture *tex = texture_make(w, h);
	if (!tex) return NULL;

	/* A stroke cannot survive its buffers being swapped - see stroke_begin for what an
	 * orphaned one does. Nothing opens one across an undo or a resize; this is the net. */
	if (t->stroke) stroke_cancel(t);

	Uint32 *old = t->pixels;

	if (t->tex) SDL_DestroyTexture(t->tex);
	t->tex       = tex;
	t->pixels    = pixels;
	t->w         = w;
	t->h         = h;

	draw_buffers_free(t);   /* the geometry moved out from under them */

	/* A new texture has seen nothing, and the rectangle that was owed belonged to the old
	 * geometry. */
	t->tx0 = t->ty0 = t->tx1 = t->ty1 = 0;
	touch_all(t);
	return old;
}

Uint32 *vng_tab_resize_raw (VNG_TAB *t, int w, int h, int dx, int dy)
{
	if (!t || w < 1 || h < 1) return NULL;

	/* The wall is checked before a byte is asked for - see vng_tab_side_limit. */
	int limit = vng_tab_side_limit();
	if (w > limit || h > limit) return NULL;

	/* Before the copy, not only in adopt: a write-through stroke's pixels are IN the buffer
	 * being copied, and cancelling it after the copy would carry them into the new one. */
	if (t->stroke) stroke_cancel(t);

	size_t  n   = (size_t)w * h;
	Uint32 *buf = (Uint32 *) SDL_malloc(n * sizeof(Uint32));
	if (!buf) return NULL;

	for (size_t i = 0; i < n; i++)
		buf[i] = 0xFFFFFFFFu;

	/* The overlap of the old rectangle placed at (dx, dy) with the new one. Computed
	 * once instead of testing every pixel: a 4000x4000 canvas is sixteen million
	 * bounds checks otherwise, and the loop is a memcpy per row without them.
	 *
	 * There may be NO overlap - a canvas moved wholly off its own pixels - and then there is
	 * nothing to copy: a negative width handed to memcpy is a very large one. */
	int x0 = dx > 0 ? dx : 0;
	int y0 = dy > 0 ? dy : 0;
	int x1 = dx + t->w < w ? dx + t->w : w;
	int y1 = dy + t->h < h ? dy + t->h : h;

	if (x1 > x0)
		for (int y = y0; y < y1; y++)
			SDL_memcpy(buf + (size_t)y * w + x0,
			           t->pixels + (size_t)(y - dy) * t->w + (x0 - dx),
			           (size_t)(x1 - x0) * sizeof(Uint32));

	/* Hands the old buffer out instead of freeing it - which is what makes recording a
	 * resize for undo cost nothing at all. A caller that does not want it frees it. */
	Uint32 *old = vng_tab_adopt(t, buf, w, h);
	if (!old) { SDL_free(buf); return NULL; }
	return old;
}

bool vng_tab_resize (VNG_TAB *t, int w, int h, int dx, int dy)
{
	if (!t) return false;

	int was_w = t->w, was_h = t->h;

	Uint32 *was = vng_tab_resize_raw(t, w, h, dx, dy);
	if (!was) return false;

	t->dirty = true;
	vng_tab_title();

	/* THE MARKED RECTANGLE IS A RECTANGLE OF THIS DRAWING, so it moves with the drawing: a
	 * canvas grown to the left put the old pixel 0 at dx, and a selection left where it was
	 * would now be holding every tool to pixels nobody chose. Undo moves it back. */
	select_shift(t, dx, dy);

	/* THE BUFFER IS NOT FREED - it becomes the undo record, and the record is therefore
	 * free. Recomputing it would be impossible anyway: shrinking a canvas destroys the
	 * pixels outside the new edge, and nothing but a copy can bring them back. */
	undo_resize(t, was, was_w, was_h, w, h, dx, dy);
	return true;
}

VNG_TAB *vng_tab_by_id (Uint32 id)
{
	for (VNG_TAB *p = vng_tabs; p; p = p->next)
		if (p->id == id) return p;
	return NULL;
}

void vng_tab_set_path (VNG_TAB *t, const char *path)
{
	if (!t || !path) return;

	/* Copied BEFORE the old one is freed: save-as can hand back the path the tab
	 * already has, and freeing it first would name the tab from freed memory. */
	char *copy = SDL_strdup(path);
	if (!copy) return;

	SDL_free(t->path);
	t->path = copy;
	tab_set_name(t, copy);
}

int vng_tab_count (void)
{
	int n = 0;
	for (VNG_TAB *p = vng_tabs; p; p = p->next) n++;
	return n;
}

int vng_tab_index (VNG_TAB *t)
{
	int i = 1;
	for (VNG_TAB *p = vng_tabs; p; p = p->next, i++)
		if (p == t) return i;
	return 0;
}

/*
 * THE TITLE BAR IS THE TAB BAR.
 *
 * The operating system already draws a strip at the top of the window carrying the
 * program's name, for free and outside the usable area. As long as there is no font to
 * draw text with, and as long as the rule is to spend no screen pixel on chrome, that
 * strip is where the tab list fits: which document, which position, how many in total,
 * and whether it is dirty.
 *
 *     landscape.png (2/3) *
 */
void vng_tab_title (void)
{
	char buf[160];

	if (!vng_tab) {
		SDL_SetWindowTitle(vng_win, "Vangopix");
		return;
	}

	int n = vng_tab_count();
	if (n > 1)
		SDL_snprintf(buf, sizeof buf, "%s (%d/%d)%s", vng_tab->name,
		             vng_tab_index(vng_tab), n, vng_tab->dirty ? " *" : "");
	else
		SDL_snprintf(buf, sizeof buf, "%s%s", vng_tab->name,
		             vng_tab->dirty ? " *" : "");

	SDL_SetWindowTitle(vng_win, buf);
}

void vng_tabs_free (void)
{
	VNG_TAB *p = vng_tabs;
	while (p) {
		VNG_TAB *n = p->next;
		tab_destroy(p);
		p = n;
	}
	vng_tabs = tail = vng_tab = NULL;
}
