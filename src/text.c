/*
 * Adapted from SKNE_CORE/src/text/text.c. Two things changed on the way in:
 *
 *   - the font bytes come from SDL_LoadFile instead of the engine's asset pack, so this
 *     module depends on SDL and stb_truetype and on nothing else;
 *   - allocation goes through SDL_malloc / SDL_free rather than the libc pair, which
 *     keeps every allocation in this program on one allocator across the three
 *     platforms.
 */

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <SDL3/SDL.h>
#include <stdarg.h>

#include "text.h"

#define ATLAS_W    512
#define ATLAS_H    512
#define FIRST_CHAR 32
#define NUM_CHARS  96

struct TextSystem {
	SDL_Renderer     *renderer;
	SDL_Texture      *atlas;
	stbtt_packedchar  chars[NUM_CHARS];
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

	TextSystem *ts = (TextSystem *) SDL_calloc(1, sizeof *ts);
	if (!ts) { SDL_free(font_data); return NULL; }
	ts->renderer  = renderer;
	ts->font_size = font_size;

	unsigned char *bitmap = (unsigned char *) SDL_calloc(ATLAS_W * ATLAS_H, 1);
	if (!bitmap) { SDL_free(font_data); SDL_free(ts); return NULL; }

	stbtt_pack_context pc;
	stbtt_PackBegin(&pc, bitmap, ATLAS_W, ATLAS_H, 0, 1, NULL);
	/* Oversampling 2x2: stb rasterises at twice the resolution and downsamples, which
	 * is what keeps small sizes from turning into mush. Costs atlas area, not frames. */
	stbtt_PackSetOversampling(&pc, 2, 2);
	int ok = stbtt_PackFontRange(&pc, font_data, 0, font_size,
	                             FIRST_CHAR, NUM_CHARS, ts->chars);
	stbtt_PackEnd(&pc);

	if (!ok)
		SDL_Log("text_init: atlas too small for the face at %.0fpx", font_size);

	stbtt_fontinfo info;
	stbtt_InitFont(&info, font_data, 0);
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
	for (int i = 0; i < ATLAS_W * ATLAS_H; i++) {
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

	while (*text) {

		if (*text == '\n') {
			cx  = x;
			cy += ts->line_height;
			text++;
			continue;
		}

		int c = (unsigned char)*text;
		if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) {
			text++;
			continue;
		}

		stbtt_aligned_quad q;
		stbtt_GetPackedQuad(ts->chars, ATLAS_W, ATLAS_H, c - FIRST_CHAR, &cx, &cy, &q, 0);

		SDL_FRect src = { q.s0 * ATLAS_W, q.t0 * ATLAS_H,
		                  (q.s1 - q.s0) * ATLAS_W, (q.t1 - q.t0) * ATLAS_H };
		SDL_FRect dst = { q.x0, q.y0, q.x1 - q.x0, q.y1 - q.y0 };

		SDL_RenderTexture(ts->renderer, ts->atlas, &src, &dst);
		text++;
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

	/* Walks the same advance stb would apply while drawing, without emitting quads. */
	while (*text) {
		if (*text == '\n') {
			if (cx > max_x) max_x = cx;
			cx = 0;
			lines++;
			text++;
			continue;
		}
		int c = (unsigned char)*text;
		if (c >= FIRST_CHAR && c < FIRST_CHAR + NUM_CHARS) {
			stbtt_aligned_quad q;
			stbtt_GetPackedQuad(ts->chars, ATLAS_W, ATLAS_H, c - FIRST_CHAR,
			                    &cx, &cy, &q, 0);
		}
		text++;
	}
	if (cx > max_x) max_x = cx;
	if (out_w) *out_w = max_x;
	if (out_h) *out_h = (float)lines * ts->line_height;
}

void text_fit (TextSystem *ts, char *dst, size_t cap, const char *src, float max_w)
{
	if (!dst || cap == 0) return;

	SDL_strlcpy(dst, src ? src : "", cap);
	if (!ts) return;

	float w, h;
	text_measure(ts, dst, &w, &h);
	if (w <= max_w) return;

	/* One character shorter each pass, with the new last column replaced rather than
	 * appended - appending would make the string grow back to the width just rejected. */
	size_t len = SDL_strlen(dst);
	while (len > 1) {
		dst[--len]   = '\0';
		dst[len - 1] = '~';
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

void text_free (TextSystem *ts)
{
	if (!ts) return;
	SDL_DestroyTexture(ts->atlas);
	SDL_free(ts);
}
