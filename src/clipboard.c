#include "clipboard.h"
#include "vangopix.h"
#include "tabs.h"

#include <SDL3_image/SDL_image.h>

/* The last copy made HERE. It outlives its place on the system clipboard on purpose - see
   clipboard.h, "when the system holds no image". */
static Uint32 *copy_px = NULL;
static int     copy_w = 0, copy_h = 0;

/* Whether the system still holds that copy. Set when it is offered, cleared by the offer's
   cleanup - which SDL calls when another program copies, and when this one offers again. */
static bool on_system  = false;
static bool local_only = false;

/* The copy encoded, made the first time something asks for it and kept until the offer
   goes: SDL does not free what the callback returns. */
static void  *enc_png = NULL, *enc_bmp = NULL;
static size_t enc_png_n = 0,  enc_bmp_n = 0;

/*
 * What a paste looks for, in the order it prefers them. png first because it is the one
 * that carries alpha as a matter of course; bmp is what a Windows screenshot arrives as
 * (SDL turns CF_DIB into it); tiff is what macOS puts a screenshot on the pasteboard as.
 * The rest are there because a browser copies an image in whatever the page served.
 */
static const char *const READ[] = {
	"image/png", "image/bmp", "image/tiff", "image/webp", "image/gif", "image/jpeg",
};

/* What is offered, in order - and on Windows only the first of these gets out. */
static const char *const OFFER[] = { "image/png", "image/bmp" };

static void enc_free (void)
{
	SDL_free(enc_png);
	SDL_free(enc_bmp);
	enc_png = enc_bmp = NULL;
	enc_png_n = enc_bmp_n = 0;
}

/* The copy as an image file in memory. The surface wraps the buffer with no conversion,
   which is the ARGB8888 decision paying out again - file.c saves the same way. */
static void *encode (bool png, size_t *n)
{
	*n = 0;
	SDL_Surface *s = SDL_CreateSurfaceFrom(copy_w, copy_h, SDL_PIXELFORMAT_ARGB8888,
	                                       copy_px, copy_w * 4);
	SDL_IOStream *io = SDL_IOFromDynamicMem();
	if (!s || !io) {
		SDL_DestroySurface(s);
		if (io) SDL_CloseIO(io);
		return NULL;
	}

	bool ok = png ? IMG_SavePNG_IO(s, io, false) : SDL_SaveBMP_IO(s, io, false);
	SDL_DestroySurface(s);

	void *mem = NULL;
	if (ok) {
		Sint64 size = SDL_GetIOSize(io);
		SDL_PropertiesID p = SDL_GetIOProperties(io);
		mem = SDL_GetPointerProperty(p, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
		/* Setting it to NULL is how the memory is taken out of the stream: closing it
		   would otherwise free what is about to be handed to the system. */
		if (mem && size > 0) {
			SDL_SetPointerProperty(p, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
			*n = (size_t)size;
		} else {
			mem = NULL;
		}
	}
	SDL_CloseIO(io);
	return mem;
}

/*
 * Asked by the system for one of the types offered. On Windows that happens INSIDE
 * SDL_SetClipboardData, and on X11, Wayland and macOS from the event pump - all of them the
 * main thread, which is what lets this read copy_px without a lock.
 */
static const void *SDLCALL offer (void *userdata, const char *mime, size_t *size)
{
	(void)userdata;
	*size = 0;
	if (!mime || !copy_px) return NULL;

	if (SDL_strcmp(mime, "image/png") == 0) {
		if (!enc_png) enc_png = encode(true, &enc_png_n);
		*size = enc_png_n;
		return enc_png;
	}
	if (SDL_strcmp(mime, "image/bmp") == 0) {
		if (!enc_bmp) enc_bmp = encode(false, &enc_bmp_n);
		*size = enc_bmp_n;
		return enc_bmp;
	}
	return NULL;
}

/* The offer is gone: another program copied, or this one is offering something new. */
static void SDLCALL offer_gone (void *userdata)
{
	(void)userdata;
	on_system = false;
	enc_free();
}

void clipboard_local (bool on)
{
	local_only = on;
}

void clipboard_put (Uint32 *px, int w, int h)
{
	if (!px || w < 1 || h < 1) { SDL_free(px); return; }

	enc_free();   /* encodings of the copy being replaced */
	SDL_free(copy_px);
	copy_px = px;
	copy_w  = w;
	copy_h  = h;

	if (local_only) return;

	/* The old offer's cleanup runs inside this call and clears the flag, so it is set after. */
	if (SDL_SetClipboardData(offer, offer_gone, NULL, OFFER, SDL_arraysize(OFFER)))
		on_system = true;
	else
		SDL_Log("clipboard: %s", SDL_GetError());
}

Uint32 *clipboard_decode (const void *data, size_t size, int limit, int *w, int *h)
{
	*w = *h = 0;
	if (!data || size == 0) return NULL;

	SDL_Surface *raw = IMG_Load_IO(SDL_IOFromConstMem(data, size), true);
	if (!raw) return NULL;

	*w = raw->w;
	*h = raw->h;

	/* Asked before the conversion, for vng_tab_open's reason: finding out after a full
	   size copy is finding out slowly. */
	if (raw->w > limit || raw->h > limit) {
		SDL_DestroySurface(raw);
		return NULL;
	}

	/* A 32-bit DIB whose alpha is all zero - what a Windows screenshot usually is - comes out
	   OPAQUE, not invisible: SDL's bmp reader treats an alpha channel with nothing in it as
	   no alpha channel (CorrectAlphaChannel, SDL_bmp.c). Pinned by test/checks.c. */
	SDL_Surface *img = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
	SDL_DestroySurface(raw);
	if (!img) { *w = *h = 0; return NULL; }

	Uint32 *out = (Uint32 *) SDL_malloc((size_t)img->w * img->h * sizeof(Uint32));
	if (out)
		/* Row by row: a surface's pitch may pad the end of each line. */
		for (int y = 0; y < img->h; y++)
			SDL_memcpy(out + (size_t)y * img->w,
			           (Uint8 *)img->pixels + (size_t)y * img->pitch,
			           (size_t)img->w * sizeof(Uint32));
	else
		*w = *h = 0;

	SDL_DestroySurface(img);
	return out;
}

/* The first image the system holds that SDL3_image can read. NULL if none - or if one was too
   big, which is said in a box, since a picture on the clipboard that pastes as nothing reads
   as CTRL+V being broken. */
static Uint32 *from_system (int *w, int *h)
{
	int limit = vng_tab_side_limit();

	for (size_t i = 0; i < SDL_arraysize(READ); i++) {
		if (!SDL_HasClipboardData(READ[i])) continue;

		size_t n = 0;
		void *data = SDL_GetClipboardData(READ[i], &n);
		Uint32 *px = clipboard_decode(data, n, limit, w, h);
		SDL_free(data);
		if (px) return px;

		if (*w > limit || *h > limit) {
			char msg[256];
			SDL_snprintf(msg, sizeof msg,
			             "The image on the clipboard is %d x %d, and this machine can show "
			             "at most %d pixels a side.", *w, *h, limit);
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, VNG_NAME, msg, vng_win);
			return NULL;
		}
		/* Unreadable in this type - a tiff with no libtiff beside the exe - so try the next. */
	}
	return NULL;
}

Uint32 *clipboard_take (int *w, int *h)
{
	*w = *h = 0;

	if (!local_only && !on_system) {
		Uint32 *px = from_system(w, h);
		if (px) return px;
		if (*w > 0) return NULL;   /* there WAS an image, and it has been refused out loud */
	}

	if (!copy_px) return NULL;

	size_t n = (size_t)copy_w * copy_h;
	Uint32 *out = (Uint32 *) SDL_malloc(n * sizeof(Uint32));
	if (!out) return NULL;

	SDL_memcpy(out, copy_px, n * sizeof(Uint32));
	*w = copy_w;
	*h = copy_h;
	return out;
}

/* The offer is LEFT with the system. SDL_Quit cancels it without emptying the clipboard, and
   on Windows the png was written out when it was offered - so a copy made here can still be
   pasted elsewhere after the program has closed. offer() answers nothing once copy_px goes. */
void clipboard_free (void)
{
	enc_free();
	SDL_free(copy_px);
	copy_px = NULL;
	copy_w = copy_h = 0;
}
