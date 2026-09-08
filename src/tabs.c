#include "tabs.h"
#include "undo.h"
#include "select.h"

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
 * what you are able to save. */
static void tab_set_name (VNG_TAB *t, const char *path)
{
	const char *base = path;
	for (const char *p = path; *p; p++)
		if (*p == '/' || *p == '\\')
			base = p + 1;
	SDL_strlcpy(t->name, base, sizeof t->name);
}

static void tab_link (VNG_TAB *t)
{
	t->prev = tail;
	t->next = NULL;
	if (tail) tail->next = t;
	else      vng_tabs = t;
	tail = t;
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
	t->tex_dirty = true;
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

bool vng_tab_stroke_open (VNG_TAB *t, bool direct)
{
	if (!t || !draw_buffers_make(t)) return false;
	if (!undo_open(t)) return false;

	t->direct = direct;

	/* An empty box, stated so that the first put widens it in both directions. */
	t->sx0 = t->w; t->sy0 = t->h;
	t->sx1 = 0;    t->sy1 = 0;
	t->stroke = true;
	return true;
}

bool vng_tab_touched (VNG_TAB *t, int x, int y)
{
	if (!t || !t->mask || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
	return t->mask[(size_t)y * t->w + x] != 0;
}

void vng_tab_put (VNG_TAB *t, int x, int y, Uint32 argb)
{
	if (!t || !t->stroke || x < 0 || y < 0 || x >= t->w || y >= t->h) return;

	size_t i = (size_t)y * t->w + x;

	if (t->direct) {
		/* Straight into the document, carry first. There is no preview to composite and
		 * nothing to merge later - which is the whole point, since what this stroke lays
		 * cannot be shown by drawing it over anything. */
		undo_carry(t, x, y, t->pixels[i], argb);
		t->pixels[i] = argb;
		t->tex_dirty = true;
	} else {
		t->pixels_preview[i] = argb;
	}

	t->mask[i] = 1;   /* the mask does its job either way: one write per pixel per stroke */

	if (x     < t->sx0) t->sx0 = x;
	if (y     < t->sy0) t->sy0 = y;
	if (x + 1 > t->sx1) t->sx1 = x + 1;
	if (y + 1 > t->sy1) t->sy1 = y + 1;
}

/* Clears the touched rectangle in both buffers and puts the same rectangle back on the GPU,
 * so the preview texture is transparent everywhere again - the invariant the partial upload
 * during a stroke depends on. */
static void preview_wipe (VNG_TAB *t)
{
	if (t->sx1 <= t->sx0 || t->sy1 <= t->sy0) return;

	for (int y = t->sy0; y < t->sy1; y++) {
		size_t row = (size_t)y * t->w;
		SDL_memset(t->pixels_preview + row + t->sx0, 0,
		           (size_t)(t->sx1 - t->sx0) * sizeof(Uint32));
		SDL_memset(t->mask + row + t->sx0, 0, (size_t)(t->sx1 - t->sx0));
	}

	SDL_Rect r = { t->sx0, t->sy0, t->sx1 - t->sx0, t->sy1 - t->sy0 };
	SDL_UpdateTexture(t->tex_preview, &r,
	                  t->pixels_preview + (size_t)r.y * t->w + r.x,
	                  t->w * (int)sizeof(Uint32));

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

			undo_carry(t, x, y, t->pixels[i], t->pixels_preview[i]);
			t->pixels[i] = t->pixels_preview[i];

			/* Left clean for the next stroke, so opening one costs nothing. */
			t->pixels_preview[i] = 0;
			t->mask[i]           = 0;
		}
	}

	if (t->sx1 > t->sx0 && t->sy1 > t->sy0) {
		/* The same rectangle, now transparent, goes back to the GPU: the preview
		 * texture is transparent everywhere again, which is the invariant the
		 * partial upload during a stroke depends on. */
		SDL_Rect r = { t->sx0, t->sy0, t->sx1 - t->sx0, t->sy1 - t->sy0 };
		SDL_UpdateTexture(t->tex_preview, &r,
		                  t->pixels_preview + (size_t)r.y * t->w + r.x,
		                  t->w * (int)sizeof(Uint32));
		t->tex_dirty = true;
	}

	undo_close(t);
}

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

VNG_TAB *vng_tab_new (int w, int h)
{
	VNG_TAB *t = tab_alloc(w, h);
	if (!t) return NULL;

	for (int i = 0; i < w * h; i++)
		t->pixels[i] = 0xFFFFFFFFu;   /* the sheet is born white and opaque */

	SDL_snprintf(t->name, sizeof t->name, "untitled %u", untitled_seq++);

	if (!tab_make_texture(t)) { SDL_free(t->pixels); SDL_free(t); return NULL; }

	tab_link(t);
	vng_tab_show(t);
	return t;
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
	SDL_Surface *raw = IMG_Load(path);
	if (!raw) {
		SDL_Log("IMG_Load(%s): %s", path, SDL_GetError());
		return NULL;
	}
	SDL_Log("opened: %s  %dx%d  source format: %s",
	        path, raw->w, raw->h, SDL_GetPixelFormatName(raw->format));

	SDL_Surface *img = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
	SDL_DestroySurface(raw);
	if (!img) {
		SDL_Log("SDL_ConvertSurface: %s", SDL_GetError());
		return NULL;
	}

	VNG_TAB *t = tab_alloc(img->w, img->h);
	if (!t) { SDL_DestroySurface(img); return NULL; }

	/* Row by row: a surface's pitch may carry padding at the end of each line, and a
	 * single memcpy would drag that garbage into the document. */
	for (int y = 0; y < t->h; y++)
		SDL_memcpy(t->pixels + (size_t)y * t->w,
		           (Uint8 *)img->pixels + (size_t)y * img->pitch,
		           (size_t)t->w * sizeof(Uint32));
	SDL_DestroySurface(img);

	t->path = SDL_strdup(path);
	tab_set_name(t, path);

	if (!tab_make_texture(t)) {
		SDL_free(t->path); SDL_free(t->pixels); SDL_free(t);
		return NULL;
	}

	tab_link(t);
	vng_tab_show(t);
	return t;
}

/*
 * Closing the LAST tab opens a blank sheet in its place.
 *
 * It is the only way to have no "no document" state - and in an editor whose entire
 * interface is the sheet, that state would be an empty window with nothing to click
 * and nothing explaining what to do. The program always has paper.
 */
void vng_tab_close (VNG_TAB *t)
{
	if (!t) return;

	VNG_TAB *fallback = t->next ? t->next : t->prev;

	if (t->prev) t->prev->next = t->next; else vng_tabs = t->next;
	if (t->next) t->next->prev = t->prev; else tail     = t->prev;

	if (t->tex) SDL_DestroyTexture(t->tex);
	draw_buffers_free(t);
	undo_free(t->undo);
	select_free(t->sel);
	SDL_free(t->pixels);
	SDL_free(t->path);
	SDL_free(t);

	if (fallback) {
		vng_tab = fallback;
		vng_tab_title();
	} else {
		vng_tab = NULL;
		vng_tab_new(64, 64);
	}
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

void vng_tab_show (VNG_TAB *t)
{
	if (!t || t == vng_tab) return;

	/* The float is put down before the sheet under it goes away. */
	select_commit(vng_tab);

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

	Uint32 *old = t->pixels;

	if (t->tex) SDL_DestroyTexture(t->tex);
	t->tex       = tex;
	t->pixels    = pixels;
	t->w         = w;
	t->h         = h;
	t->tex_dirty = true;

	draw_buffers_free(t);   /* the geometry moved out from under them */
	return old;
}

Uint32 *vng_tab_resize_raw (VNG_TAB *t, int w, int h, int dx, int dy)
{
	if (!t || w < 1 || h < 1) return NULL;

	Uint32 *buf = (Uint32 *) SDL_malloc((size_t)w * h * sizeof(Uint32));
	if (!buf) return NULL;

	for (int i = 0; i < w * h; i++)
		buf[i] = 0xFFFFFFFFu;

	/* The overlap of the old rectangle placed at (dx, dy) with the new one. Computed
	 * once instead of testing every pixel: a 4000x4000 canvas is sixteen million
	 * bounds checks otherwise, and the loop is a memcpy per row without them. */
	int x0 = dx > 0 ? dx : 0;
	int y0 = dy > 0 ? dy : 0;
	int x1 = dx + t->w < w ? dx + t->w : w;
	int y1 = dy + t->h < h ? dy + t->h : h;

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
	int was_w = t->w, was_h = t->h;

	Uint32 *was = vng_tab_resize_raw(t, w, h, dx, dy);
	if (!was) return false;

	t->dirty = true;
	vng_tab_title();

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
		if (p->tex) SDL_DestroyTexture(p->tex);
		draw_buffers_free(p);
		undo_free(p->undo);
		select_free(p->sel);
		SDL_free(p->pixels);
		SDL_free(p->path);
		SDL_free(p);
		p = n;
	}
	vng_tabs = tail = vng_tab = NULL;
}
