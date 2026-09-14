/*
 * Adapted from SKNE_CORE/src/text/text.c. Three things changed on the way in:
 *
 *   - the font bytes come from SDL_LoadFile instead of the engine's asset pack, so this
 *     module depends on SDL and stb_truetype and on nothing else;
 *   - allocation goes through SDL_malloc / SDL_free rather than the libc pair, which
 *     keeps every allocation in this program on one allocator across the three
 *     platforms - stb_truetype's own included, which is why SDL comes in first and the
 *     STBTT_ macros are set before the implementation is: without them it quietly called
 *     malloc, free, assert and libm behind the promise this line makes;
 *   - the atlas holds LATIN-1 as well as ASCII, and strings are read as UTF-8 - see
 *     text.h. A folder called "Área de Trabalho" came out as "rea de Trabalho".
 */

#include <SDL3/SDL.h>
#include <stdarg.h>

#define STBTT_malloc(x, u)  ((void)(u), SDL_malloc(x))
#define STBTT_free(x, u)    ((void)(u), SDL_free(x))
#define STBTT_assert(x)     SDL_assert(x)
#define STBTT_ifloor(x)     ((int) SDL_floor(x))
#define STBTT_iceil(x)      ((int) SDL_ceil(x))
#define STBTT_sqrt(x)       SDL_sqrt(x)
#define STBTT_pow(x, y)     SDL_pow(x, y)
#define STBTT_fmod(x, y)    SDL_fmod(x, y)
#define STBTT_cos(x)        SDL_cos(x)
#define STBTT_acos(x)       SDL_acos(x)
#define STBTT_fabs(x)       SDL_fabs(x)
#define STBTT_strlen(x)     SDL_strlen(x)
#define STBTT_memcpy        SDL_memcpy
#define STBTT_memset        SDL_memset

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "text.h"

/*
 * 1024 SQUARE, AND IT WAS 512 UNTIL THE ATLAS HELD MORE THAN ASCII.
 *
 * Two ranges of ninety-six glyphs, oversampled twice each way, at the largest size config.txt
 * allows (32) need more room than a 512 atlas has; 1024 holds both with rows to spare. It is a
 * texture of four megabytes per face, made once.
 */
#define ATLAS_W     1024
#define ATLAS_H     1024

/* The printable ASCII range, and the top half of Latin-1: the accented letters of Portuguese,
   Spanish, French and German, and the few signs beside them (NBSP to y-diaeresis). */
#define ASCII_FIRST 32
#define ASCII_LOT   96
#define LATIN_FIRST 0xA0
#define LATIN_LOT   96

struct TextSystem {
	SDL_Renderer     *renderer;   /* SDL.h's typedef of the struct text.h names by its tag */
	SDL_Texture      *atlas;
	stbtt_packedchar  ascii[ASCII_LOT];
	stbtt_packedchar  latin[LATIN_LOT];
	float             font_size;
	float             line_height;
	float             ascent;
};

TextSystem *text_init (SDL_Renderer *renderer, const char *font_path, float font_size)
{
	size_t         font_len  = 0;
	unsigned char *font_data = (unsigned char *) SDL_LoadFile(font_path, &font_len);

	if (!font_data) {
		SDL_Log("text_init: cannot open '%s': %s", font_path, SDL_GetError());
		return NULL;
	}

	/*
	 * THE FILE IS ASKED WHETHER IT IS A FONT BEFORE ANYTHING READS IT AS ONE.
	 *
	 * stb_truetype takes the bytes on trust: handed a truncated or corrupt file, the packer
	 * went ahead - it does not check its own InitFont - and read tables at whatever offsets the
	 * garbage named, and then the metrics below did the same. A face that will not initialise is
	 * a face that failed to load, which is a supported state: text_draw on NULL draws nothing.
	 * Twelve bytes is the smallest header there is.
	 */
	stbtt_fontinfo info;
	int offset = font_len >= 12 ? stbtt_GetFontOffsetForIndex(font_data, 0) : -1;

	if (offset < 0 || !stbtt_InitFont(&info, font_data, offset)) {
		SDL_Log("text_init: '%s' is not a face this can read", font_path);
		SDL_free(font_data);
		return NULL;
	}

	TextSystem *ts = (TextSystem *) SDL_calloc(1, sizeof *ts);
	if (!ts) { SDL_free(font_data); return NULL; }
	ts->renderer  = renderer;
	ts->font_size = font_size;

	unsigned char *bitmap = (unsigned char *) SDL_calloc((size_t)ATLAS_W * ATLAS_H, 1);
	if (!bitmap) { SDL_free(font_data); SDL_free(ts); return NULL; }

	stbtt_pack_range ranges[2];
	SDL_zeroa(ranges);
	ranges[0].font_size                        = font_size;
	ranges[0].first_unicode_codepoint_in_range = ASCII_FIRST;
	ranges[0].num_chars                        = ASCII_LOT;
	ranges[0].chardata_for_range               = ts->ascii;
	ranges[1].font_size                        = font_size;
	ranges[1].first_unicode_codepoint_in_range = LATIN_FIRST;
	ranges[1].num_chars                        = LATIN_LOT;
	ranges[1].chardata_for_range               = ts->latin;

	stbtt_pack_context pc;
	stbtt_PackBegin(&pc, bitmap, ATLAS_W, ATLAS_H, 0, 1, NULL);
	/* Oversampling 2x2: stb rasterises at twice the resolution and downsamples, which
	 * is what keeps small sizes from turning into mush. Costs atlas area, not frames. */
	stbtt_PackSetOversampling(&pc, 2, 2);
	int ok = stbtt_PackFontRanges(&pc, font_data, 0, ranges, 2);
	stbtt_PackEnd(&pc);

	/* A glyph that did not fit is left with no size and draws as nothing; the rest are good.
	 * Worth a line, since it only happens at a size past what the atlas was made for. */
	if (!ok)
		SDL_Log("text_init: atlas too small for the face at %.0fpx", font_size);

	int ascent, descent, line_gap;
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
	float scale = stbtt_ScaleForPixelHeight(&info, font_size);
	ts->line_height = (float)(ascent - descent + line_gap) * scale;
	ts->ascent      = (float)ascent * scale;

	/* stb hands back one coverage byte per pixel. The texture is white with that byte
	 * as alpha, so a single SDL_SetTextureColorMod recolours every glyph at draw time -
	 * one atlas serves every colour the program will ever use. */
	unsigned char *rgba = (unsigned char *) SDL_malloc((size_t)ATLAS_W * ATLAS_H * 4);
	if (!rgba) { SDL_free(bitmap); SDL_free(font_data); SDL_free(ts); return NULL; }
	for (size_t i = 0; i < (size_t)ATLAS_W * ATLAS_H; i++) {
		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = bitmap[i];
	}

	ts->atlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
	                              SDL_TEXTUREACCESS_STATIC, ATLAS_W, ATLAS_H);
	if (!ts->atlas) {
		SDL_Log("text_init: SDL_CreateTexture failed: %s", SDL_GetError());
		SDL_free(rgba); SDL_free(bitmap); SDL_free(font_data); SDL_free(ts);
		return NULL;
	}
	SDL_SetTextureBlendMode(ts->atlas, SDL_BLENDMODE_BLEND);
	SDL_UpdateTexture(ts->atlas, NULL, rgba, ATLAS_W * 4);

	SDL_free(rgba);
	SDL_free(bitmap);
	SDL_free(font_data);
	return ts;
}

/*
 * THE QUAD FOR ONE CHARACTER, AND THE PEN MOVED PAST IT.
 *
 * False for a control character, which draws nothing and moves nothing - a tab or a carriage
 * return in a name is not a letter. A character the atlas does not hold is drawn as a QUESTION
 * MARK rather than skipped: skipping is how "Área" came out "rea", a name that looked whole and
 * was not. A mark in its place says there was a letter there this face could not show.
 */
static bool quad_of (TextSystem *ts, Uint32 cp, float *cx, float *cy, stbtt_aligned_quad *q)
{
	if (cp < ASCII_FIRST) return false;

	if (cp < ASCII_FIRST + ASCII_LOT)
		stbtt_GetPackedQuad(ts->ascii, ATLAS_W, ATLAS_H, (int)(cp - ASCII_FIRST), cx, cy, q, 0);
	else if (cp >= LATIN_FIRST && cp < LATIN_FIRST + LATIN_LOT)
		stbtt_GetPackedQuad(ts->latin, ATLAS_W, ATLAS_H, (int)(cp - LATIN_FIRST), cx, cy, q, 0);
	else
		stbtt_GetPackedQuad(ts->ascii, ATLAS_W, ATLAS_H, '?' - ASCII_FIRST, cx, cy, q, 0);
	return true;
}

void text_draw (TextSystem *ts, const char *text, float x, float y, uint32_t color)
{
	if (!ts || !text) return;

	SDL_SetTextureColorMod(ts->atlas, (Uint8)(color >> 24), (Uint8)(color >> 16),
	                                  (Uint8)(color >> 8));
	SDL_SetTextureAlphaMod(ts->atlas, (Uint8)(color));

	/* y is the top of the line, but stb advances along the BASELINE - hence + ascent.
	 * Skipping this makes every string sit one ascent too high, which reads as a
	 * mysterious vertical offset rather than as an error. */
	float cx = x, cy = y + ts->ascent;

	/* A CHARACTER AT A TIME, NOT A BYTE: SDL_StepUTF8 reads one whole code point and moves
	 * past it, and hands back U+FFFD for bytes that are not UTF-8 - which then draws as the
	 * question mark, like anything else the atlas does not hold. */
	while (*text) {
		Uint32 cp = SDL_StepUTF8(&text, NULL);

		if (cp == '\n') {
			cx  = x;
			cy += ts->line_height;
			continue;
		}

		stbtt_aligned_quad q;
		if (!quad_of(ts, cp, &cx, &cy, &q)) continue;

		SDL_FRect src = { q.s0 * ATLAS_W, q.t0 * ATLAS_H,
		                  (q.s1 - q.s0) * ATLAS_W, (q.t1 - q.t0) * ATLAS_H };
		SDL_FRect dst = { q.x0, q.y0, q.x1 - q.x0, q.y1 - q.y0 };

		SDL_RenderTexture(ts->renderer, ts->atlas, &src, &dst);
	}
}

void text_measure (TextSystem *ts, const char *text, float *out_w, float *out_h)
{
	if (!ts || !text) {
		if (out_w) *out_w = 0;
		if (out_h) *out_h = 0;
		return;
	}

	float cx = 0, cy = 0, max_x = 0;
	int   lines = 1;

	/* Walks the same advance stb would apply while drawing, without emitting quads - and the
	 * same characters text_draw would, so a measured string and a drawn one cannot disagree. */
	while (*text) {
		Uint32 cp = SDL_StepUTF8(&text, NULL);

		if (cp == '\n') {
			if (cx > max_x) max_x = cx;
			cx = 0;
			lines++;
			continue;
		}

		stbtt_aligned_quad q;
		quad_of(ts, cp, &cx, &cy, &q);
	}
	if (cx > max_x) max_x = cx;
	if (out_w) *out_w = max_x;
	if (out_h) *out_h = (float)lines * ts->line_height;
}

/* Where the character ending at byte `end` begins: back over the continuation bytes (10xxxxxx)
   to its first. */
static size_t char_start (const char *s, size_t end)
{
	if (end == 0) return 0;

	size_t i = end - 1;
	while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
	return i;
}

void text_fit (TextSystem *ts, char *dst, size_t cap, const char *src, float max_w)
{
	if (!dst || cap == 0) return;

	/* Copied whole characters only, so a name cut by the buffer is not cut inside a letter. */
	SDL_utf8strlcpy(dst, src ? src : "", cap);
	if (!ts) return;

	float w, h;
	text_measure(ts, dst, &w, &h);
	if (w <= max_w) return;

	/* One CHARACTER shorter each pass, with the new last one replaced by the tilde rather than
	 * appended to - appending would make the string grow back to the width just rejected. A
	 * character and not a byte: a cut through the middle of an accented letter left half of it,
	 * which is not UTF-8 at all. */
	size_t len = SDL_strlen(dst);
	for (;;) {
		size_t gone = char_start(dst, len);   /* the last character goes */
		if (gone == 0) return;                /* one left - it is the tilde already */

		size_t last = char_start(dst, gone);  /* and the one before it becomes the tilde */
		dst[last]     = '~';
		dst[last + 1] = '\0';
		len           = last + 1;

		text_measure(ts, dst, &w, &h);
		if (w <= max_w) return;
	}
}

void text_cell (TextSystem *ts, float *out_w, float *out_h)
{
	if (!ts) {
		if (out_w) *out_w = 0;
		if (out_h) *out_h = 0;
		return;
	}
	float w, h;
	text_measure(ts, "M", &w, &h);
	if (out_w) *out_w = w;
	if (out_h) *out_h = ts->line_height;
	(void)h;
}

float text_line_height (TextSystem *ts)
{
	return ts ? ts->line_height : 0.0f;
}

void text_print (TextSystem *ts, float x, float y, uint32_t color, const char *fmt, ...)
{
	char    buf[4096];
	va_list ap;

	va_start(ap, fmt);
	SDL_vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	text_draw(ts, buf, x, y, color);
}

void text_print_center (TextSystem *ts, float x, float y, uint32_t color,
                        const char *fmt, ...)
{
	char    buf[4096];
	va_list ap;

	va_start(ap, fmt);
	SDL_vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	float w, h;
	text_measure(ts, buf, &w, &h);
	text_draw(ts, buf, x - w * 0.5f, y - h * 0.5f, color);
}

/*
 * THE SHADOW PASS, AND WHY IT IS ONE NUMBER IN ONE PLACE.
 *
 * Both calls below are the same two draws with a different idea of where (x, y) is, so the
 * offset and the colour are stated once. Written into each of them, the day somebody decides
 * the shadow should sit two pixels away is the day half the program's text moves and the
 * other half does not.
 */
#define SHADOW_OFF  1.0f
#define SHADOW_INK  0x000000FFu

void text_print_shadow (TextSystem *ts, float x, float y, uint32_t color, const char *fmt, ...)
{
	char    buf[4096];
	va_list ap;

	va_start(ap, fmt);
	SDL_vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	/* FORMATTED ONCE AND DRAWN TWICE. Calling text_print twice with the same arguments would
	 * work and would run vsnprintf twice - but the reason it is not done that way is the
	 * other one: two calls are two argument lists to keep in step, and a shadow that says
	 * something different from its face is a bug nobody looks for. */
	text_draw(ts, buf, x + SHADOW_OFF, y + SHADOW_OFF, SHADOW_INK);
	text_draw(ts, buf, x, y, color);
}

void text_print_center_shadow (TextSystem *ts, float x, float y, uint32_t color,
                               const char *fmt, ...)
{
	char    buf[4096];
	va_list ap;

	va_start(ap, fmt);
	SDL_vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	/* Measured once as well, and both passes hang off the SAME corner. Centring each of them
	 * on its own would centre two identical blocks identically - which is a shadow exactly
	 * behind its letters, and therefore no shadow at all. */
	float w, h;
	text_measure(ts, buf, &w, &h);

	float cx = x - w * 0.5f;
	float cy = y - h * 0.5f;

	text_draw(ts, buf, cx + SHADOW_OFF, cy + SHADOW_OFF, SHADOW_INK);
	text_draw(ts, buf, cx, cy, color);
}

void text_free (TextSystem *ts)
{
	if (!ts) return;
	SDL_DestroyTexture(ts->atlas);
	SDL_free(ts);
}
