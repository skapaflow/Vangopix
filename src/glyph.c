#include "glyph.h"

/* The points come from the first Vangopix's src/gui/gui_main_tool.c, vertex for vertex.
 * They are hand placed on a grid of roughly -12..+10 around the origin, which is what lets
 * a 16 pixel offset put the whole glyph clear of the point being aimed at. */
static const SDL_FPoint vpencil[] = {
	{  6, -3},{  4, -5},{  2, -3},{  4, -1},{  2, -3},{  1, -2},{  3,  0},{  1, -2},{ -4,  3},
	{ -2,  5},{ -4,  3},{ -6,  6},{ -5,  7},{ -6,  6},{ -7,  8},{ -5,  7},{ -2,  5}
};

static const SDL_FPoint vline[] = {
	{ -6,  6},{ -6,  7},{ -5,  7},{  6, -4},{  6, -5},{  5, -5}
};

static const SDL_FPoint vrect[] = {
	{ -7, -4},{ -7,  6},{  7,  6},{  7, -4}
};

static const SDL_FPoint vellipse[] = {
	{  5, -4},{  7, -1},{  7,  3},{  5,  6},{  2,  8},{ -2,  8},{ -5,  6},{ -7,  3},{ -7, -1},
	{ -5, -4},{ -2, -6},{  2, -6}
};

static const SDL_FPoint veraser[] = {
	{ -8,  3},{  0, -3},{  8, -3},{  0,  3},{ -8,  3},{ -8,  6},{  0,  6},{  8,  0},{  8, -3},
	{  0,  3}
};

static const SDL_FPoint vbucket[] = {
	{  0, -6},{  1, -4},{  3, -3},{ -4,  4},{  3, -3},{  6,  0},{ -1,  7},{  6,  0},{  7,  1},
	{  0,  8},{ -2,  6},{ -4,  4},{ -5,  2},{ -7,  1},{ -7,  9},{ -8,  8},{ -9,  6},{ -9,  1},
	{ -8, -1},{ -6, -2},{ -4, -2},{ -7,  1},{ -4, -2}
};

static const SDL_FPoint vspray[] = {
	{ -2,  2},{  2, -2},{ -2,  2},{  3,  7},{  7,  3},{  2, -2},{ -1, -2},{ -2, -3},{ -3, -2},
	{ -7, -7},{ -7,  3},{ -3, -2},{ -2, -1},{ -1, -2},{ -2, -1}
};

static const SDL_FPoint vchange[] = {
	{ -6,  7},{  1,  7},{  6,  7},{  6, -5},{  1, -5},{  6, -5},{  6,  7},{  1,  7},{  1,  3},
	{ -1,  3},{ -1,  5},{ -5,  1},{ -1, -3},{ -1, -1},{  1, -1},{  1, -5},{ -6, -5}
};

static const SDL_FPoint vselect[] = {
	{ -5, -4},{ -7, -4},{ -7, -6},{ -5, -6},{ -5, -4},{ -5,  6},{ -7,  6},{ -7,  8},{ -5,  8},
	{ -5,  6},{  5,  6},{  7,  6},{  7,  8},{  5,  8},{  5,  6},{  5, -4},{  5, -6},{  7, -6},
	{  7, -4},{  5, -4},{  5, -4}
};

/* A plain arrow, and the only glyph here that is not a tool: it is drawn turned, and what it
 * is turned to is the whole of what it says. */
static const SDL_FPoint vpointer[] = {
	{  0, -8},{ -5,  7},{  0,  7},{  5,  7}
};

static const SDL_FPoint vpick[] = {
	{  1, -3},{  4,  0},{  1, -3},{ -4,  2},{ -6,  4},{ -7,  6},{ -7,  8},{ -5,  8},{ -3,  7},
	{ -1,  5},{  4,  0},{  5,  1},{  6,  0},{  6, -1},{  2, -5},{  6, -1},{  8, -3},{  9, -4},
	{  9, -6},{  8, -7},{  7, -8},{  5, -8},{  4, -7},{  2, -5},{  1, -5},{  0, -4}
};

typedef struct { const SDL_FPoint *pt; int lot; } SHAPE;

/* The order is the TOOL order, so a tool indexes its own glyph with no table between. */
static const SHAPE shapes[GLYPH_LOT] = {
	{ vpencil,  (int)SDL_arraysize(vpencil)  },
	{ vline,    (int)SDL_arraysize(vline)    },
	{ vrect,    (int)SDL_arraysize(vrect)    },
	{ vellipse, (int)SDL_arraysize(vellipse) },
	{ veraser,  (int)SDL_arraysize(veraser)  },
	{ vbucket,  (int)SDL_arraysize(vbucket)  },
	{ vspray,   (int)SDL_arraysize(vspray)   },
	{ vchange,  (int)SDL_arraysize(vchange)  },
	{ vselect,  (int)SDL_arraysize(vselect)  },
	{ vpick,    (int)SDL_arraysize(vpick)    },
	{ vpointer, (int)SDL_arraysize(vpointer) },
};

/* One more than the longest glyph, because the path is closed by repeating its first point.
 * A stack array rather than an allocation: this runs once a frame, and the first Vangopix's
 * malloc per draw was the one thing about draw_wireframe_entity worth leaving behind. */
#define MAX_PT 64

static void path (const SDL_FPoint *pt, int lot, float x, float y, float rot, float scale,
                  Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	SDL_FPoint out[MAX_PT];

	/* The first Vangopix's rotation, sign for sign, so a glyph turned to 90 here points the
	 * same way it did there. */
	float sn = SDL_sinf(rot * (SDL_PI_F / 180.0f));
	float cs = SDL_cosf(rot * (SDL_PI_F / 180.0f));

	for (int i = 0; i < lot; i++) {
		out[i].x = x + (pt[i].x *  cs + pt[i].y * sn) * scale;
		out[i].y = y + (pt[i].x * -sn + pt[i].y * cs) * scale;
	}
	out[lot] = out[0];   /* closes it */

	/* The blend is stated here rather than inherited. Nothing this file draws is translucent
	 * today - the shadow was deliberately made opaque, because three quarters of one takes the
	 * colour of whatever it lands on - but a path that accepts an alpha and does not blend it
	 * is the defect that was just found in the colour readouts, and leaving one instance of a
	 * class is how it comes back. */
	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(vng_ren, r, g, b, a);
	SDL_RenderLines(vng_ren, out, lot + 1);
}

void glyph_draw (GLYPH g, float x, float y, float rot, float scale, Uint32 rgba)
{
	if (g < 0 || g >= GLYPH_LOT) return;

	const SHAPE *s = &shapes[g];
	if (s->lot + 1 > MAX_PT) return;

	/*
	 * The shadow first, one pixel down and right, so the colour draws over it rather than
	 * under it. Without this pass a white glyph is invisible on white artwork.
	 *
	 * OPAQUE, not a wash. It was 0xC0, and a shadow at three quarters is a shadow that takes
	 * the colour of whatever it lands on - over the hue ring it went red, green or blue and
	 * stopped separating the glyph from the band at all. The point of this pass is to be the
	 * one thing under the outline that is the same everywhere.
	 */
	path(s->pt, s->lot, x + 1.0f, y + 1.0f, rot, scale, 0x00, 0x00, 0x00, 0xFF);

	path(s->pt, s->lot, x, y, rot, scale,
	     (Uint8)((rgba >> 24) & 0xFF), (Uint8)((rgba >> 16) & 0xFF),
	     (Uint8)((rgba >>  8) & 0xFF), (Uint8)( rgba        & 0xFF));
}
