#include "field.h"
#include "expr.h"
#include "ui.h"
#include "keys.h"
#include "primitives.h"

/* The box: dark inside, a rim that brightens while it is open, and the caret a hairline. The
 * colour window's hex box had these and they are its numbers. */
#define BOX_BG    0xFF0C0C0Cu
#define BOX_RIM   0xFF383838u
#define BOX_LIVE  0xFFC0C0C0u    /* the rim while the field holds the keyboard */
#define BOX_TEXT  0xDCDCDCFFu    /* 0xRRGGBBAA - text_print's order, not the primitives' */
#define BOX_LABEL 0xFF8000FFu
#define PAD        ui_pad()
#define GAP        ui_pad()      /* between a label and its box */

struct _vng_field_ {
	char           text[VNG_FIELD_MAX];
	char           was[VNG_FIELD_MAX];  /* what it opened holding - what ESC puts back */
	VNG_FIELD_KIND kind;
	bool           open;
	size_t         caret;        /* where the next character goes - 0 .. strlen(text) */
	bool           live;         /* report every keystroke, not only ENTER - see field.h */

	void (*done) (const char *, void *);
	FIELD_STEP step;      /* what TAB means here, or NULL for a box alone - see field.h */
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

void field_step (VNG_FIELD *f, FIELD_STEP fn) { if (f) f->step = fn; }
void field_live (VNG_FIELD *f, bool on)       { if (f) f->live = on; }

/*
 * Hands the text over WITHOUT letting go of the keyboard. What a live box does on every key,
 * and what TAB does on its way to the next box - both are reports, not endings.
 *
 * A NUMBER BOX HANDS OVER THE ANSWER, NOT THE SUM. The owner asked for a number and gets one;
 * `32*4` reaches it as `128` and nothing downstream has to know arithmetic happened. That is
 * what makes this free for every window that already has a field - anim.c did not change a
 * line for it.
 *
 * AND A LINE THAT IS NOT A SUM YET REPORTS NOTHING. A box being typed into spends most of its
 * life holding half an expression, so `32*` says nothing at all and the owner keeps the value
 * it had - which is the only version of this that can run on every keystroke without the
 * number under the hand flickering through the halves of what is being typed.
 */
static void tell (VNG_FIELD *f)
{
	if (!f->done) return;

	if (f->kind != VNG_FIELD_INT && f->kind != VNG_FIELD_REAL) {
		f->done(f->text, f->ctx);
		return;
	}

	double v;
	if (!expr_eval(f->text, &v)) return;

	/* Out of range is refused rather than wrapped: casting a double past INT_MAX to int is
	 * undefined, and the number it would land on is nobody's answer. */
	if (v > 2.0e9 || v < -2.0e9) return;

	char out[VNG_FIELD_MAX];

	/* AN INT BOX ROUNDS AND A SPEED BOX DOES NOT. 160/3 into a frame count is 53, not 53.33 -
	 * the box was asked for a whole number and truncation would make every division read one
	 * short of what the hand meant. */
	if (f->kind == VNG_FIELD_INT) SDL_snprintf(out, sizeof out, "%d", (int)SDL_lround(v));
	else                          SDL_snprintf(out, sizeof out, "%g", v);

	f->done(out, f->ctx);
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

	/* Kept so ESC has something to put back. Only a live box ever needs it, but a box that
	 * remembers what it was handed regardless is one less thing to keep in step. */
	SDL_strlcpy(f->was, f->text, sizeof f->was);

	/* AT THE END, which is where the original resolved its -1 to and what makes typing add
	 * to what is there rather than replace it - see field.h. */
	f->caret = SDL_strlen(f->text);
	f->open  = true;
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

	/* Through tell, so ENTER answers with the same number every other route does. */
	if (commit) tell(f);
}

/* Whether this field accepts that character, given what it already holds. The kind is not
   decoration: a frame count that accepts a letter is a count that strtof quietly reads as
   zero, and then a clip has no frames and nothing says why. */
/* + - * / ( ) and room to breathe. A NUMBER BOX TAKES A SUM - see expr.h on why an offset is
   almost never a number somebody knows. */
static bool sums (char c)
{
	return c == '+' || c == '-' || c == '*' || c == '/' ||
	       c == '(' || c == ')' || c == ' ';
}

/*
 * ONE PLACE DECIDES WHAT A NUMBER IS, and after this it is not here.
 *
 * These used to enforce the shape as the keys arrived - a minus only in front, one dot and
 * only one. That rule cannot survive arithmetic, where 1.5+2.5 is two dots and 3*-2 is a minus
 * in the middle, and rewriting it to allow those would be writing half a parser by hand at the
 * keystroke. So the keystroke is only asked whether the character COULD belong to a sum, and
 * whether the line adds up to anything is expr.c's answer, once, on the whole line.
 */
static bool takes (VNG_FIELD *f, char c)
{
	switch (f->kind) {
	case VNG_FIELD_INT:
	case VNG_FIELD_REAL:
		return (c >= '0' && c <= '9') || c == '.' || sums(c);

	case VNG_FIELD_HEX:
		return SDL_isxdigit((unsigned char)c) || c == '#' || c == ' ';

	default:
		return c >= 0x20 && c < 0x7F;   /* printable ASCII - see the note in conventions */
	}
}

/* Takes out the character at `i`, terminator and all. One place, because backspace and delete
   differ only in WHICH character they mean - and writing that twice is how they drift. */
static void erase (VNG_FIELD *f, size_t i)
{
	size_t n = SDL_strlen(f->text);
	if (i >= n) return;

	for (size_t j = i; j < n; j++) f->text[j] = f->text[j + 1];
}

static void on_key (const SDL_Event *e, void *ctx)
{
	VNG_FIELD *f = (VNG_FIELD *) ctx;

	if (e->type == SDL_EVENT_TEXT_INPUT) {
		for (const char *t = e->text.text; *t; t++) {
			if (!takes(f, *t)) continue;

			size_t n = SDL_strlen(f->text);
			if (n + 1 >= sizeof f->text) break;

			/* AT THE CARET, not on the end. Everything from there right shifts up one - the
			 * terminator included, which is why the copy starts at n and not at n-1. */
			for (size_t i = n + 1; i > f->caret; i--) f->text[i] = f->text[i - 1];

			f->text[f->caret++] = *t;
		}

		/* AS IT IS TYPED, for a box whose answer is drawn where the eye already is. */
		if (f->live) tell(f);
		return;
	}
	if (e->type != SDL_EVENT_KEY_DOWN) return;

	switch (e->key.key) {

	/* The character BEFORE the caret, and the caret comes back with it. */
	case SDLK_BACKSPACE:
		if (f->caret > 0) {
			erase(f, f->caret - 1);
			f->caret--;

			/* Taking a digit away is as much a change as adding one, and a box that reported
			 * only on the way up would leave the model holding a number nobody can see. */
			if (f->live) tell(f);
		}
		break;

	/*
	 * DELETE EMPTIES THE BOX - the original's own key (print_input.c:61), and it belongs with
	 * the caret rather than despite it.
	 *
	 * Forward-delete was the obvious thing to put here and it is the wrong trade. Once typing
	 * ADDS to what is there, every box needs a fast way to say "not this, something else" -
	 * and with no select-all in this program, backspacing a colour like #FF8000FF out one
	 * character at a time is exactly the friction that made the old clear-on-first-key look
	 * reasonable. One key that empties the box is what buys the appending behaviour; taking
	 * out the character in front of the caret is a convenience nothing asked for.
	 */
	case SDLK_DELETE:
		if (f->text[0]) {
			f->text[0] = 0;
			f->caret   = 0;
			if (f->live) tell(f);
		}
		break;

	/*
	 * WHERE THE HAND STANDS IN THE STRING. Clamped at both ends and not wrapped, the same as
	 * the original's __max/__min (print_input.c:74-75) and the same as TAB across the form:
	 * an end is a thing worth being able to feel.
	 */
	case SDLK_LEFT:  if (f->caret > 0) f->caret--;                     break;
	case SDLK_RIGHT: if (f->caret < SDL_strlen(f->text)) f->caret++;   break;
	case SDLK_HOME:  f->caret = 0;                                     break;
	case SDLK_END:   f->caret = SDL_strlen(f->text);                   break;
	case SDLK_RETURN:
	case SDLK_KP_ENTER: field_close(f, true);  break;
	case SDLK_ESCAPE:
		/* A live box has already told the model everything it typed, so closing quietly is
		 * not the same as cancelling any more. Putting back what it opened holding is - and
		 * that is what keeps ESC free, which is the whole reason it is offered. */
		if (f->live) {
			SDL_strlcpy(f->text, f->was, sizeof f->text);
			f->caret = SDL_strlen(f->text);
			tell(f);
		}
		field_close(f, false);
		break;

	/*
	 * TAB WALKS THE FORM, SHIFT+TAB WALKS IT BACK - and both LEAVE this box, which means both
	 * take what is in it. Read out of the field before closing, because closing is what hands
	 * the value to the owner and the owner is free to have moved on by then.
	 *
	 * Nothing happens without a step handler: a box alone has nowhere to go, and a TAB that
	 * quietly committed and closed it would be a keystroke doing something nobody asked for.
	 * Nor does it fall through to the sidebar - keys.c consumes every key while a field is
	 * open, which is the whole reason that module exists.
	 */
	case SDLK_TAB: {
		if (!f->step) break;

		/* THE VALUE GOES ACROSS BEFORE THE NEIGHBOUR OPENS, and the order is the whole of it:
		 * opening the neighbour drops this field WITHOUT committing - that is what a click
		 * into another box has to mean - and the neighbour then reads a model this box has
		 * already written to. Committed by hand rather than by closing, because closing is
		 * exactly what must not happen yet. */
		tell(f);

		/* And then the owner is simply asked to move. NOWHERE TO GO IS NOT A MOVE: at the end
		 * of a form the owner opens nothing, so this box is still open, still holding the
		 * keyboard, caret where the hand left it. That is what the original's __max/__min
		 * clamp did, and it is why nothing is closed up here on the way past. */
		f->step((e->key.mod & SDL_KMOD_SHIFT) ? -1 : +1, f->ctx);
		break;
	}
	default: break;
	}
}

bool field_press (VNG_FIELD *f, SDL_FRect r, float x, float y, const char *show)
{
	if (!f) return false;

	if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) return false;

	/* IT OPENS HOLDING WHAT IT WAS SHOWING, and not its own buffer. The two are the same only
	 * after somebody has typed into this box once; before that the buffer is empty, so a box
	 * reading out a live value BLANKED ITSELF the moment it was clicked - which is how the
	 * animation editor lost every number it was displaying. field_draw's `show` and this one
	 * are the same value on purpose: one box, one answer, both directions. */
	if (!f->open) field_open(f, show ? show : f->text);
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

	float ty    = box.y + SDL_floorf((box.h - th) * 0.5f);
	float room  = box.w - PAD * 2.0f;

	/*
	 * A CLOSED BOX READS OUT; AN OPEN ONE IS BEING EDITED, and those want different cuts.
	 *
	 * Closed, the value is a whole thing and text_fit's tilde from the right is the house rule
	 * every name in this program follows. Open, the only part that MUST be on screen is the
	 * part under the caret - cut from the right and a hand typing past the edge is typing
	 * blind, watching a tilde. So the window slides: the first character shown is pushed
	 * rightwards until what is left fits, which is the plainest thing that keeps the caret in
	 * view and needs no state to remember between frames.
	 */
	if (!live_now) {
		char cut[VNG_FIELD_MAX + 8];
		text_fit(vng_text, cut, sizeof cut, str, room);
		text_print(vng_text, box.x + PAD, ty, BOX_TEXT, "%s", cut);
		return;
	}

	size_t from = 0;
	float  lead = 0.0f;   /* how wide the text is from `from` up to the caret */

	for (;;) {
		char head[VNG_FIELD_MAX];
		SDL_strlcpy(head, f->text + from, f->caret - from + 1);

		text_measure(vng_text, head, &lead, NULL);

		/* One past the caret, so the caret itself always has a pixel to stand on. */
		if (lead <= room - 1.0f || from >= f->caret) break;
		from++;
	}

	char cut[VNG_FIELD_MAX + 8];
	text_fit(vng_text, cut, sizeof cut, f->text + from, room);
	text_print(vng_text, box.x + PAD, ty, BOX_TEXT, "%s", cut);

	/* BETWEEN THE CHARACTERS AND NOT AFTER THE LAST ONE. It used to be drawn past the whole
	 * string, which was right only while the only edit anybody could make was at the end. */
	SDL_FRect caret = { box.x + PAD + lead, box.y + 2.0f, 1.0f, box.h - 4.0f };
	prim_fill(caret, 0xE0FFFFFFu);
}
