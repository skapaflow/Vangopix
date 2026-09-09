#include "field.h"
#include "keys.h"
#include "primitives.h"

/* The box: dark inside, a rim that brightens while it is open, and the caret a hairline. The
 * colour window's hex box had these and they are its numbers. */
#define BOX_BG    0xFF0C0C0Cu
#define BOX_RIM   0xFF383838u
#define BOX_LIVE  0xFFC0C0C0u    /* the rim while the field holds the keyboard */
#define BOX_TEXT  0xDCDCDCFFu    /* 0xRRGGBBAA - text_print's order, not the primitives' */
#define BOX_LABEL 0xFF8000FFu
#define PAD        4.0f
#define GAP        4.0f          /* between a label and its box */

struct _vng_field_ {
	char           text[VNG_FIELD_MAX];
	VNG_FIELD_KIND kind;
	bool           open;
	bool           fresh;        /* the next key replaces rather than appends */

	void (*done) (const char *, void *);
	void  *ctx;
};

/*
 * ONE FIELD AT A TIME, PROGRAM-WIDE, and it is not a convenience - keys.c does not stack
 * owners, so a second field taking the keyboard silently drops the first one's claim. Holding
 * the open one here means the drop is explicit, and it is what lets a form of seven boxes have
 * no focus ring to keep: the open field IS the focus.
 */
static VNG_FIELD *live = NULL;

static void on_key (const SDL_Event *e, void *ctx);

VNG_FIELD *field_make (VNG_FIELD_KIND kind, void (*done)(const char *, void *), void *ctx)
{
	VNG_FIELD *f = (VNG_FIELD *) SDL_calloc(1, sizeof *f);
	if (!f) return NULL;

	f->kind = kind;
	f->done = done;
	f->ctx  = ctx;
	return f;
}

void field_free (VNG_FIELD *f)
{
	if (!f) return;
	if (live == f) field_close(f, false);
	SDL_free(f);
}

bool        field_open_p (VNG_FIELD *f) { return f && f->open; }
const char *field_text   (VNG_FIELD *f) { return f ? f->text : ""; }

void field_open (VNG_FIELD *f, const char *text)
{
	if (!f) return;

	/* Whatever was open loses the keyboard, and keeps what was typed into it rather than
	 * committing it - a click into another box is not an ENTER. */
	if (live && live != f) field_close(live, false);

	SDL_strlcpy(f->text, text ? text : "", sizeof f->text);
	f->open  = true;
	f->fresh = true;
	live     = f;

	/* keys_capture also calls SDL_StartTextInput, without which SDL3 delivers no
	 * SDL_EVENT_TEXT_INPUT at all and the IME never activates. Deriving characters from
	 * keycodes instead works on exactly one keyboard layout. */
	keys_capture(on_key, f);
}

void field_close (VNG_FIELD *f, bool commit)
{
	if (!f || !f->open) return;

	f->open = false;
	keys_release(f);
	if (live == f) live = NULL;

	if (commit && f->done) f->done(f->text, f->ctx);
}

/* Whether this field accepts that character, given what it already holds. The kind is not
   decoration: a frame count that accepts a letter is a count that strtof quietly reads as
   zero, and then a clip has no frames and nothing says why. */
static bool takes (VNG_FIELD *f, char c)
{
	switch (f->kind) {
	case VNG_FIELD_INT:
		if (c >= '0' && c <= '9') return true;
		return c == '-' && f->text[0] == 0;

	case VNG_FIELD_REAL:
		if (c >= '0' && c <= '9') return true;
		if (c == '-') return f->text[0] == 0;
		return c == '.' && !SDL_strchr(f->text, '.');   /* one dot, and only one */

	case VNG_FIELD_HEX:
		return SDL_isxdigit((unsigned char)c) || c == '#' || c == ' ';

	default:
		return c >= 0x20 && c < 0x7F;   /* printable ASCII - see the note in conventions */
	}
}

static void on_key (const SDL_Event *e, void *ctx)
{
	VNG_FIELD *f = (VNG_FIELD *) ctx;

	if (e->type == SDL_EVENT_TEXT_INPUT) {
		for (const char *t = e->text.text; *t; t++) {
			if (!takes(f, *t)) continue;

			/* The replace happens on the first ACCEPTED key, not on the first key seen - or a
			 * rejected letter would silently blank a number the hand meant to keep. */
			if (f->fresh) { f->text[0] = 0; f->fresh = false; }

			size_t n = SDL_strlen(f->text);
			if (n + 1 >= sizeof f->text) break;

			f->text[n]     = *t;
			f->text[n + 1] = 0;
		}
		return;
	}
	if (e->type != SDL_EVENT_KEY_DOWN) return;

	switch (e->key.key) {
	case SDLK_BACKSPACE: {
		size_t n = SDL_strlen(f->text);
		if (n) f->text[n - 1] = 0;
		f->fresh = false;   /* backspace means "I am editing this one" */
		break;
	}
	case SDLK_RETURN:
	case SDLK_KP_ENTER: field_close(f, true);  break;
	case SDLK_ESCAPE:   field_close(f, false); break;
	default: break;
	}
}

bool field_press (VNG_FIELD *f, SDL_FRect r, float x, float y)
{
	if (!f) return false;

	if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) return false;

	if (!f->open) field_open(f, f->text);
	return true;
}

/*
 * A NULL FIELD STILL DRAWS, and that is not defensiveness - it is the contract.
 *
 * `show` is what a box reads out while it is not being typed into, and a field that does not
 * exist yet is not being typed into. Returning early on NULL made the colour window's hex
 * readout INVISIBLE UNTIL IT WAS FIRST CLICKED, because the field was built lazily in the
 * press handler: the box that exists to answer "what colour is this" answered nothing until
 * somebody asked it a different question.
 *
 * The field is made with its window now, so that path is gone - but a readout that vanishes
 * when nobody has typed is a bug worth making impossible rather than fixing once.
 */
void field_draw (VNG_FIELD *f, SDL_FRect r, const char *label, const char *show)
{
	if (!vng_text) return;

	SDL_FRect box = r;

	/* THE LABEL IS IN THE SMALL FACE AND THE BOX GIVES IT ROOM, which is what lets seven of
	 * these stack into a form with no layout system in between: each one is handed a
	 * rectangle and divides it itself. */
	if (label && *label) {
		float lw, lh;
		text_measure(vng_text_small, label, &lw, &lh);

		text_print(vng_text_small, r.x, r.y + SDL_floorf((r.h - lh) * 0.5f),
		           BOX_LABEL, "%s", label);

		box.x += lw + GAP;
		box.w -= lw + GAP;
	}
	if (box.w < 8.0f) return;

	bool live_now = f && f->open;

	prim_fill(box, BOX_BG);
	prim_rect(box, live_now ? BOX_LIVE : BOX_RIM);

	/* WHAT IT SHOWS WHEN IT IS CLOSED IS NOT ITS OWN TEXT, and that is the point of `show`: a
	 * box can read out a live value it does not own - the colour in the slot, the size of the
	 * clip being played - and BECOME that value the moment it is typed into. One box that
	 * answers in both directions beats a label with an input under it. */
	const char *str = live_now ? f->text : (show ? show : (f ? f->text : ""));

	float th;
	text_measure(vng_text, "M", NULL, &th);

	char cut[VNG_FIELD_MAX + 8];
	text_fit(vng_text, cut, sizeof cut, str, box.w - PAD * 2.0f);

	float ty = box.y + SDL_floorf((box.h - th) * 0.5f);
	text_print(vng_text, box.x + PAD, ty, BOX_TEXT, "%s", cut);

	if (!live_now) return;

	float tw;
	text_measure(vng_text, cut, &tw, NULL);

	SDL_FRect caret = { box.x + PAD + tw + 1.0f, box.y + 2.0f, 1.0f, box.h - 4.0f };
	prim_fill(caret, 0xE0FFFFFFu);
}
