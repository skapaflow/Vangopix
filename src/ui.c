#include "ui.h"

/*
 * WHAT THESE ARE WHEN THERE IS NO FONT.
 *
 * A missing face is not fatal anywhere in this program - text_draw on a NULL system is a
 * no-op by design - so the layout has to keep working without one. It also matters for the
 * checks, which never call vangopix_init and therefore run with vng_text NULL: a vocabulary
 * that answered zero there would put every window at nothing by nothing and make the whole
 * of it untestable.
 *
 * The numbers are what the shipped face measures at its default size, so a fontless run lays
 * out the same as a normal one and simply draws no letters.
 */
#define NO_FONT_LINE   16.0f
#define NO_FONT_SMALL  11.0f
#define NO_FONT_CELL    8.0f

/* Rounded, because every one of these ends up as a rectangle edge and the primitives snap to
   whole pixels anyway - rounding here means the arithmetic that stacks them agrees with what
   lands on screen. */
static float whole (float v) { return SDL_floorf(v + 0.5f); }

float ui_line (void)
{
	float h = vng_text ? text_line_height(vng_text) : 0.0f;
	return h > 1.0f ? whole(h) : NO_FONT_LINE;
}

float ui_small (void)
{
	float h = vng_text_small ? text_line_height(vng_text_small) : 0.0f;
	return h > 1.0f ? whole(h) : NO_FONT_SMALL;
}

float ui_cell (void)
{
	float w = 0.0f, h = 0.0f;

	if (vng_text) text_cell(vng_text, &w, &h);
	return w > 1.0f ? whole(w) : NO_FONT_CELL;
}

/* A quarter of a line, and never less than two - at a very small face a pad of one is not a
   gap, it is a rounding error. */
float ui_pad (void)
{
	float p = whole(ui_line() * 0.25f);
	return p < 2.0f ? 2.0f : p;
}

float ui_row  (void) { return ui_line() + ui_pad() * 2.0f; }
float ui_head (void) { return ui_line() + ui_pad() * 2.0f; }

/* Square, and the height of a line: it is a letter in a bar sized for letters. The original
   was a flat 14 in a bar that had already grown to 20. */
float ui_close (void) { return ui_line(); }

/* Three quarters of a line, which is where the hand-picked 12 already sat against a 16 line -
   so this changes nothing today and keeps up when the face moves. */
float ui_grip (void) { return whole(ui_line() * 0.75f); }
