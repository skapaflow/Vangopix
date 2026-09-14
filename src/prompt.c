#include "prompt.h"
#include "field.h"
#include "ui.h"
#include "style.h"

#define BOX_W   320.0f   /* the least it is; a longer question widens it */

/*
 * THE LINE IS A FIELD NOW, which prompt.h said the day would come for.
 *
 * It was its own little editor - a buffer, a blinking bar, backspace written out a second time -
 * and it was a keyboard owner that nothing else in the program knew about. That second part is
 * what made it a trap: keys.c does not stack owners, so when a window's box was clicked while
 * this was open, the box took the keyboard and this never heard - its question stayed drawn in
 * the middle of the window for good, holding nothing. As a VNG_FIELD it is closed by the same
 * rule that closes every other box when another one opens, and it gets the caret, the arrows,
 * HOME, END and DELETE the boxes already had.
 */
static VNG_FIELD  *box   = NULL;
static char        label[64];
static PROMPT_DONE done  = NULL;

/* ENTER, through the field - which has already let go of the keyboard by the time this runs, so
   the answer is free to open another question if it wants to. */
static void on_done (const char *text, void *ctx)
{
	(void)ctx;

	PROMPT_DONE d = done;
	if (d) d(text);
}

bool prompt_open (const char *label_text, const char *initial, PROMPT_DONE fn)
{
	if (!vng_text) return false;

	if (!box) box = field_make(VNG_FIELD_TEXT, on_done, NULL);
	if (!box) return false;

	SDL_strlcpy(label, label_text ? label_text : "", sizeof label);
	done = fn;

	field_open(box, initial ? initial : "");
	return true;
}

void prompt_draw (void)
{
	if (!field_open_p(box)) return;

	float lh  = ui_line();
	float pad = ui_pad() * 2.0f;

	float lw = 0.0f;
	text_measure(vng_text, label, &lw, NULL);

	float bw = lw + pad * 2.0f > BOX_W ? lw + pad * 2.0f : BOX_W;
	float bh = pad * 3.0f + lh + ui_row();

	/* Rounded, or the box lands on a half pixel and its one pixel border comes out
	 * grey on one side and absent on the other. */
	float bx = SDL_roundf((vng_win_w - bw) * 0.5f);
	float by = SDL_roundf((vng_win_h - bh) * 0.5f);

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	SDL_FRect panel = { bx, by, bw, bh };
	/* panel_color, at the prompt's own opacity. SDL's rect rather than prim_fill, for the
	 * rounding the comment above is about: the border is drawn the same way beside it. */
	style_ink(style_alpha(vng_style.panel, 0xF4));
	SDL_RenderFillRect(vng_ren, &panel);
	SDL_SetRenderDrawColor(vng_ren, 0x50, 0x50, 0x50, 0xFF);
	SDL_RenderRect(vng_ren, &panel);

	text_print(vng_text, bx + pad, by + pad, style_rgba(vng_style.text_dim), "%s", label);

	SDL_FRect line = { bx + pad, by + pad * 2.0f + lh, bw - pad * 2.0f, ui_row() };
	field_draw(box, line, NULL, NULL);
}

bool prompt_up (void) { return field_open_p(box); }

void prompt_free (void)
{
	field_free(box);
	box = NULL;
}
