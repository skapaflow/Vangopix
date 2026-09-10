#include "splash.h"
#include "ui.h"

/*
 * The picture, and where its writable band begins.
 *
 * BAND_Y IS A FACT ABOUT THE FILE, not a choice made here: the art has a white strip across
 * the bottom 64 pixels, and 448 is where it starts. Stated as a number because there is
 * nothing in a PNG that says "the words go here" - and stated ONCE, so the day the art is
 * redrawn there is a single line to move.
 */
#define SPLASH_PNG  "icon/vangopix_splash_screen.png"
#define BAND_Y      445.0f

/*
 * THE YEAR COMES FROM THE BUILD, so it cannot go stale.
 *
 * __DATE__ is "Mmm dd yyyy" and the year is the last four characters of it. A year typed into
 * the source is a year that is right until January and wrong for the eleven months nobody
 * looks at it - and a splash screen is exactly the thing nobody looks at twice.
 */
#define BUILD_YEAR  (&__DATE__[7])

static SDL_Texture *shot = NULL;    /* the picture and its words, composed as one */
static float        shot_w = 0.0f;
static float        shot_h = 0.0f;
static bool         showing = false;

bool splash_up (void) { return showing && shot != NULL; }

/*
 * WRITES THE BAND. Dark on white, because the band is white - the one place in this program
 * where that is true, and the reason these colours are here rather than from the interface's
 * own palette, which is built for a dark ground.
 *
 * Laid out from the band's own edges rather than from typed offsets: the name and what it is
 * hang from the left, the version and the year from the right, and the way out sits under
 * them both. Change the art's band height and this follows it.
 */
static void band (float w, float h) {

	if (!vng_text) return;

	const float pad  = ui_pad() * 2.0f;
	const float line = ui_line();
	const float top  = BAND_Y + pad;

	const Uint32 WHITE  = 0xFFFFFFFFu;
	// const Uint32 INK  = 0x202020FFu;
	// const Uint32 SOFT = 0x808080FFu;

	(void)h;

	text_print_shadow(vng_text, pad, top, WHITE, "Vangopix");
	text_print_shadow(vng_text_small, pad, top + line, WHITE, "a pixel art editor");

	/* Right-aligned, so the two numbers stack against the edge whatever their width. */
	{
		char ver[32], year[16];
		float vw, yw;

		SDL_snprintf(ver,  sizeof ver,  "%s", VNG_VERSION);
		SDL_snprintf(year, sizeof year, "%s", BUILD_YEAR);

		text_measure(vng_text,       ver,  &vw, NULL);
		text_measure(vng_text_small, year, &yw, NULL);

		text_print_shadow(vng_text,       w - pad - vw, top,        WHITE,  "%s", ver);
		text_print_shadow(vng_text_small, w - pad - yw, top + line, WHITE, "%s", year);
	}

	/*
	 * AND HOW TO LEAVE, which is the one line here that is not decoration: a picture that
	 * covers the program and does not say what dismisses it is a picture somebody waits out.
	 */
	{
		const char *out = "any key or click to begin";
		float tw;

		text_measure(vng_text_small, out, &tw, NULL);
		text_print_shadow(vng_text_small, SDL_floorf((w - tw) * 0.5f), top + line * 2.0f, WHITE, "%s", out);
	}
}

void splash_open (void) {

	char *path = vangopix_asset(SPLASH_PNG);
	if (!path) return;

	SDL_Surface *raw = IMG_Load(path);
	SDL_free(path);

	/* Not fatal, and not logged as an error either: a build without the art beside it starts
	 * on the desk, which is a working program - see splash.h. */
	if (!raw) return;

	SDL_Texture *art = SDL_CreateTextureFromSurface(vng_ren, raw);
	float w = (float)raw->w, h = (float)raw->h;
	SDL_DestroySurface(raw);

	if (!art) return;

	/*
	 * COMPOSED ONTO A TARGET, art first and words on top, so the two are one texture from
	 * here on. Everything after this point moves and scales them together - see splash.h on
	 * why that is the whole reason the words are not simply drawn beside the picture.
	 */
	shot = SDL_CreateTexture(vng_ren, SDL_PIXELFORMAT_ARGB8888,
							 SDL_TEXTUREACCESS_TARGET, (int)w, (int)h);
	if (!shot) { SDL_DestroyTexture(art); return; }

	SDL_Texture *was = SDL_GetRenderTarget(vng_ren);
	SDL_SetRenderTarget(vng_ren, shot);

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(vng_ren, 0, 0, 0, 0);
	SDL_RenderClear(vng_ren);
	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	SDL_RenderTexture(vng_ren, art, NULL, NULL);
	band(w, h);

	SDL_SetRenderTarget(vng_ren, was);
	SDL_DestroyTexture(art);

	SDL_SetTextureBlendMode(shot, SDL_BLENDMODE_BLEND);

	shot_w  = w;
	shot_h  = h;
	showing = true;
}

bool splash_event (const SDL_Event *e) {

	if (!splash_up()) return false;

	switch (e->type) {

	/*
	 * IT LEAVES ON THE RELEASE, and takes the release with it. Dismissing on the press would
	 * leave the matching release to land on whatever was behind - which, at startup, is the
	 * project strip sitting under the middle of the screen's own left edge.
	 */
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		showing = false;
		return true;

	/* Swallowed but not acted on: the press half of the gesture above, and anything else a
	 * hand can do to a window it is not being invited to use yet. */
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_TEXT_INPUT:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_WHEEL:
		return true;

	/*
	 * EVERYTHING ELSE FALLS THROUGH, and the important one is SDL_EVENT_QUIT: the window's X
	 * and ALT+F4 have to work while this is up, or the splash is a way to trap somebody in
	 * the program. It is the same rule prompt.c keeps for its own modal line.
	 */
	default:
		return false;
	}
}

void splash_draw (void) {

	if (!splash_up()) return;

	SDL_SetRenderDrawBlendMode(vng_ren, SDL_BLENDMODE_BLEND);

	/* A wash over what is behind. Without it the picture floats on the checkerboard and reads
	 * as something that failed to close rather than as something in front. */
	SDL_FRect all = { 0.0f, 0.0f, (float)vng_win_w, (float)vng_win_h };
	SDL_SetRenderDrawColor(vng_ren, 0x00, 0x00, 0x00, 0xA0);
	SDL_RenderFillRect(vng_ren, &all);

	/*
	 * ITS OWN SIZE, CENTRED - and shrunk only when the window is smaller than it is. Scaling
	 * a 448 pixel picture up to fill a maximised window is how a splash ends up as a wall of
	 * soft edges; the middle of a big screen is where the eye already is.
	 */
	float k = 1.0f;
	float room_w = (float)vng_win_w - ui_pad() * 4.0f;
	float room_h = (float)vng_win_h - ui_pad() * 4.0f;

	if (shot_w > room_w) k = room_w / shot_w;
	if (shot_h * k > room_h) k = room_h / shot_h;
	if (k < 0.05f) k = 0.05f;

	float w = SDL_floorf(shot_w * k);
	float h = SDL_floorf(shot_h * k);

	SDL_FRect dst = {
		SDL_floorf(((float)vng_win_w - w) * 0.5f),
		SDL_floorf(((float)vng_win_h - h) * 0.5f),
		w, h
	};

	/* Linear only when it is being shrunk. At 1:1 the two are the same call, and nearest on a
	 * reduced image throws away whole rows - the rule core.c states for the sheet. */
	SDL_SetTextureScaleMode(shot, k < 1.0f ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
	SDL_RenderTexture(vng_ren, shot, NULL, &dst);
}

void splash_free (void) {

	if (shot) SDL_DestroyTexture(shot);
	shot    = NULL;
	showing = false;
}