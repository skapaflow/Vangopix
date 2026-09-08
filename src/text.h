#ifndef VANGOPIX_TEXT_H
#define VANGOPIX_TEXT_H

#include <stdint.h>
#include <stddef.h>

/*
 * Text rendering, adapted from the text module of the Skyonara engine (SKNE_CORE).
 *
 * The glyphs of a TrueType face are packed once into a 512x512 atlas with stb_truetype
 * and uploaded as a single texture; drawing a string is then one SDL_RenderTexture per
 * character out of that atlas. No per-string texture, no per-frame rasterising.
 *
 * WHY THIS AND NOT SDL_ttf: SDL_ttf would be a third dll to ship on three platforms,
 * and it rasterises to a surface per string, which then has to become a texture. Here
 * the only cost after startup is drawing quads, and stb_truetype is a header - it
 * crosses to Linux and macOS with the source, not with a package manager.
 */

typedef struct SDL_Renderer SDL_Renderer;
typedef struct TextSystem   TextSystem;

/* Loads the face at font_path, packs the printable ASCII range into the atlas and
   uploads it. Returns NULL on failure, having logged the reason. */
TextSystem *text_init (SDL_Renderer *renderer, const char *font_path, float font_size);

/* Draws text at (x, y), the top-left of the first line. Handles '\n'.
   color is 0xRRGGBBAA. */
void text_draw (TextSystem *ts, const char *text, float x, float y, uint32_t color);

/* printf into a draw. color is 0xRRGGBBAA. */
void text_print (TextSystem *ts, float x, float y, uint32_t color, const char *fmt, ...);

/* Same, but (x, y) is the geometric centre of the block. */
void text_print_center (TextSystem *ts, float x, float y, uint32_t color,
                        const char *fmt, ...);

/* Width and height text would occupy, without drawing it. */
void text_measure (TextSystem *ts, const char *text, float *out_w, float *out_h);

/* Copies src into dst, shortened until it fits max_w, with the last column turned into
 * a tilde.
 *
 * A TILDE AND NOT AN ELLIPSIS. It came from VagrantUI, and it outlived the decision to use
 * VagrantUI at all: two truncation marks in one program read as two different kinds of
 * truncation, whoever draws them. It lives here rather than in each panel for the same
 * reason - the tab bar, the sidebar and a window title must cut a long name the same way.
 *
 * dst always ends up NUL terminated, and is safe to draw even when max_w is absurd. */
void text_fit (TextSystem *ts, char *dst, size_t cap, const char *src, float max_w);

/* The cell of one character, measured on 'M'.
 *
 * ONLY MEANINGFUL FOR A MONOSPACED FACE. Anything laying content out in columns needs one
 * fixed char_w/char_h; give it the cell of a proportional face and every column drifts. The
 * face Vangopix ships is monospaced (DejaVu Sans Mono), so this is true by default - but the
 * fallback in vangopix.c is not, and whoever lays out columns should know which one loaded. */
void text_cell (TextSystem *ts, float *out_w, float *out_h);

/* Distance between the baselines of two consecutive lines. */
float text_line_height (TextSystem *ts);

/* Destroys the atlas texture and frees the system. */
void text_free (TextSystem *ts);

#endif
