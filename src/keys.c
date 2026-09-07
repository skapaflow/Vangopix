#include "keys.h"

static KEYS_HANDLER owner_fn  = NULL;
static void        *owner_ctx = NULL;

void keys_capture (KEYS_HANDLER fn, void *ctx)
{
	if (!fn) return;

	owner_fn  = fn;
	owner_ctx = ctx;

	/* SDL3 delivers no SDL_EVENT_TEXT_INPUT until this is called, and it is also what
	 * activates the platform's IME - which is the only way a composed character ever
	 * reaches a field. Deriving text from key events instead works on one keyboard
	 * layout, the author's. */
	SDL_StartTextInput(vng_win);
}

void keys_release (void *ctx)
{
	if (ctx != owner_ctx) return;

	owner_fn  = NULL;
	owner_ctx = NULL;
	SDL_StopTextInput(vng_win);
}

bool keys_owned (void) { return owner_fn != NULL; }

static bool is_key_event (const SDL_Event *e)
{
	switch (e->type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_TEXT_INPUT:
	/* TEXT_EDITING is the half-composed state an IME shows before the character is
	 * settled. A field that ignores it loses nothing but the preview; letting it
	 * through to the shortcuts would run one per keystroke of composition. */
	case SDL_EVENT_TEXT_EDITING:
		return true;
	default:
		return false;
	}
}

bool keys_event (const SDL_Event *e)
{
	if (!owner_fn || !is_key_event(e)) return false;

	/* The handler may release from inside this call. The event is consumed either way:
	 * the ESC that closes a field is spent on closing it, and must not go on to toggle
	 * the tab bar on its way out. */
	owner_fn(e, owner_ctx);
	return true;
}

bool keys_held (SDL_Scancode sc)
{
	if (owner_fn) return false;

	const bool *state = SDL_GetKeyboardState(NULL);
	return state && state[sc];
}

SDL_Keymod keys_mods (void)
{
	return owner_fn ? SDL_KMOD_NONE : SDL_GetModState();
}
