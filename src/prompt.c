#include "prompt.h"
#include "keys.h"

#define BOX_W   320.0f
#define PAD       8.0f
#define BLINK     0.5f   /* seconds lit, then the same dark */
#define CAP      128     /* a path is longer than this, and a path is not what this is
                          * for; a field that scrolls sideways is a decision to raise */

static bool        active = false;
static char        buf[CAP];
static char        label[64];
static PROMPT_DONE done   = NULL;
static float       blink  = 0.0f;

static void prompt_close (void)
{
	active = false;
	keys_release(&active);   /* &active is the holder's identity - a stable address that
	                          * nothing else in the program can present */
}

static void on_key (const SDL_Event *e, void *ctx)
{
	(void)ctx;

	if (e->type == SDL_EVENT_TEXT_INPUT) {
		/* The text comes from SDL and not from the keycode, which is the only version
		 * that survives a layout where the digits are on SHIFT. */
		SDL_strlcat(buf, e->text.text, sizeof buf);
		blink = 0.0f;        /* the caret is lit while the hand is moving */
		return;
	}

	if (e->type != SDL_EVENT_KEY_DOWN) return;

	switch (e->key.key) {

	case SDLK_BACKSPACE: {
		/* Back over ONE CHARACTER, not one byte: a continuation byte of a composed
		 * character is 10xxxxxx, so walking back to the first byte that is not one
		 * removes the whole thing. Deleting a byte would leave half a character in the
		 * buffer, which then goes out to whoever asked for the text. */
		size_t n = SDL_strlen(buf);
		while (n > 0) {
			n--;
			if (((unsigned char)buf[n] & 0xC0) != 0x80) break;
		}
		buf[n] = '\0';
		blink  = 0.0f;
		break;
	}

	case SDLK_RETURN:
	case SDLK_KP_ENTER: {
		/* Both are copied out and the field is closed BEFORE the callback runs: what it
		 * does is its own business, up to opening another field, and it must not find
		 * this one still holding the keyboard it is about to ask for. */
		PROMPT_DONE d = done;
		char text[CAP];
		SDL_strlcpy(text, buf, sizeof text);

		prompt_close();
		if (d) d(text);
		break;
	}

	case SDLK_ESCAPE:
		/* Consumed by having been offered here at all - see keys_event. Without the
		 * owner in front of the chain this same ESC would also toggle the tab bar. */
		prompt_close();
		break;

	default:
		break;
	}
}

bool prompt_open (const char *label_text, const char *initial, PROMPT_DONE fn)
{
	if (!vng_text) return false;

	SDL_strlcpy(label, label_text ? label_text : "", sizeof label);
	SDL_strlcpy(buf,   initial    ? initial    : "", sizeof buf);

	done   = fn;
	blink  = 0.0f;
	active = true;

	keys_capture(on_key, &active);
	return true;
}

void prompt_draw (void)
{
	if (!active) return;

	blink += vng_dt;
	if (blink >= BLINK * 2.0f) blink -= BLINK * 2.0f;

	float lh = text_line_height(vng_text);
	if (lh < 1.0f) lh = 17.0f;

	float bw = BOX_W;
	float bh = PAD * 3.0f + lh * 2.0f;
	/* Rounded, or the box lands on a half pixel and its one pixel border comes out
	 * grey on one side and absent on the other. */
	float bx = SDL_roundf((vng_win_w - bw) * 0.5f);
	float by = SDL_roundf((vng_win_h - bh) * 0.5f);

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	SDL_FRect box = { bx, by, bw, bh };
	SDL_SetRenderDrawColor(vng_ren, 0x14, 0x14, 0x14, 0xF4);
	SDL_RenderFillRect(vng_ren, &box);
	SDL_SetRenderDrawColor(vng_ren, 0x50, 0x50, 0x50, 0xFF);
	SDL_RenderRect(vng_ren, &box);

	text_print(vng_text, bx + PAD, by + PAD, 0x909090FF, "%s", label);

	float ty = by + PAD * 2.0f + lh;
	text_print(vng_text, bx + PAD, ty, 0xFFFFFFFF, "%s", buf);

	/* The caret is what says the keyboard is here and not on the sheet. It blinks
	 * because a still bar reads as a character in the text. */
	if (blink < BLINK) {
		float tw;
		text_measure(vng_text, buf, &tw, NULL);

		SDL_FRect caret = { bx + PAD + tw + 1.0f, ty + 1.0f, 1.0f, lh - 3.0f };
		SDL_SetRenderDrawColor(vng_ren, 0xFF, 0xFF, 0xFF, 0xFF);
		SDL_RenderFillRect(vng_ren, &caret);
	}
}
