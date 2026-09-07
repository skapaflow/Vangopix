#include "tabs.h"

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
static bool tab_make_texture (VNG_TAB *t)
{
	if (t->tex) SDL_DestroyTexture(t->tex);
	t->tex = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
	                           SDL_TEXTUREACCESS_STREAMING, t->w, t->h);
	if (!t->tex) {
		SDL_Log("SDL_CreateTexture: %s", SDL_GetError());
		return false;
	}
	t->tex_dirty = true;
	return true;
}

static VNG_TAB *tab_alloc (int w, int h)
{
	VNG_TAB *t = (VNG_TAB *) SDL_calloc(1, sizeof *t);
	if (!t) return NULL;

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
	vng_tab = t;
	vng_tab_title();
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
	vng_tab = t;
	vng_tab_title();
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

	vng_tab = n;
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
		SDL_free(p->pixels);
		SDL_free(p->path);
		SDL_free(p);
		p = n;
	}
	vng_tabs = tail = vng_tab = NULL;
}
