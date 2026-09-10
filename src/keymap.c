#include "keymap.h"

/*
 * ONE ROW PER ACTION, INDEXED BY IT. The name is what the file says, the description is what
 * the desk says, and the keycode is where it lives when nobody has said otherwise.
 *
 * These three belong together because they are three faces of one fact. Split across a
 * dispatch switch, a help string and a config table - which is where they were - they drift,
 * and the drift is invisible: the program keeps working and only the words go wrong.
 *
 * The order is keymap.h's enum, and the nine tools must stay contiguous and in tool order.
 */
typedef struct {
	const char *name;
	const char *does;
	SDL_Keycode key;
} BIND;

static const BIND fallback[VNG_ACT_LOT] = {
	{ "tool-pencil",    "pencil",                          SDLK_Q },
	{ "tool-line",      "line",                            SDLK_W },
	{ "tool-rect",      "rectangle",                       SDLK_E },
	{ "tool-ellipse",   "ellipse",                         SDLK_R },
	{ "tool-eraser",    "eraser",                          SDLK_A },
	{ "tool-bucket",    "bucket",                          SDLK_S },
	{ "tool-spray",     "spray",                           SDLK_D },
	{ "tool-change",    "change colours",                  SDLK_F },
	{ "tool-select",    "select",                          SDLK_Z },

	{ "colour-mix",     "mix the two in hand",             SDLK_M },

	{ "sheet-prev",     "the sheet before this one",       SDLK_1 },
	{ "sheet-next",     "the one after it",                SDLK_2 },
	{ "view-home",      "put it back: centred, 1 to 1",    SDLK_0 },

	{ "panel-projects", "projects",                        SDLK_TAB },
	{ "panel-sheets",   "the row of sheets",               SDLK_ESCAPE },
	{ "panel-colour",   "colour",                          SDLK_C },
	{ "panel-palette",  "palette",                         SDLK_P },
	{ "panel-thumb",    "the drawing at 1:1",              SDLK_V },
	{ "panel-anim",     "animation",                       SDLK_X },
	{ "overlay",        "what is on screen",               SDLK_F1 },
};

/*
 * Where each action ACTUALLY lives - the defaults until the file says otherwise.
 *
 * SEEDED ON FIRST USE AND NOT BY keymap_load, so this module is usable by anybody who never
 * called it. A keymap that is empty until somebody remembers to initialise it is a program
 * whose keyboard silently does nothing, and the thing that would catch that - the check suite
 * - is exactly the caller with no reason to load a file. It is the same rule text.c follows
 * with a NULL font: the absence of a setup call costs the setup, not the feature.
 */
static SDL_Keycode live[VNG_ACT_LOT];
static bool        seeded = false;

static void seed (void)
{
	if (seeded) return;
	seeded = true;

	for (int i = 0; i < VNG_ACT_LOT; i++) live[i] = fallback[i].key;
}

/*
 * WHAT THE DESK SHOWS, IN ORDER, and it is the built-in layout rather than the file's.
 *
 * The file is a keymap, not a page layout: it says which key, and the grouping and the
 * wording stay here. That is what stops an edited file from rearranging the help into
 * something that no longer matches the program.
 *
 *   head  != NULL   a heading
 *   act   >= 0      one binding, spelled from `live` and described from `fallback`
 *   keys  != NULL   a FIXED row - a gesture that is not a bare key and cannot be remapped
 *   all empty       a blank line
 */
typedef struct {
	const char *head;
	int         act;
	const char *keys;
	const char *does;
} SHOWROW;

#define GAP         { NULL, -1, NULL, NULL }
#define HEAD(h)     { (h), -1, NULL, NULL }
#define ACT(a)      { NULL, (a), NULL, NULL }
#define FIXED(k, d) { NULL, -1, (k), (d) }

static const SHOWROW layout[] = {
	HEAD("the sheet"),
	FIXED("CTRL N",       "a new sheet, at a size you say"),
	FIXED("CTRL O",       "open an image"),
	FIXED("CTRL S",       "save"),
	FIXED("CTRL SHIFT S", "save somewhere else"),
	FIXED("CTRL W",       "close the sheet"),
	ACT(VNG_ACT_SHEET_PREV),
	ACT(VNG_ACT_SHEET_NEXT),
	FIXED("CTRL Z  Y",    "undo, redo"),
	GAP,

	HEAD("tools"),
	ACT(VNG_ACT_TOOL_PENCIL),
	ACT(VNG_ACT_TOOL_LINE),
	ACT(VNG_ACT_TOOL_RECT),
	ACT(VNG_ACT_TOOL_ELLIPSE),
	ACT(VNG_ACT_TOOL_ERASER),
	ACT(VNG_ACT_TOOL_BUCKET),
	ACT(VNG_ACT_TOOL_SPRAY),
	ACT(VNG_ACT_TOOL_CHANGE),
	ACT(VNG_ACT_TOOL_SELECT),
	FIXED("SHIFT wheel",  "the size of the tip"),
	GAP,

	HEAD("a selection"),
	FIXED("CTRL A C X V", "all, copy, cut, paste"),
	FIXED("DEL",          "clear it"),
	FIXED("V H I R",      "flip, mirror, invert, turn"),
	GAP,

	HEAD("colour"),
	FIXED("SHIFT R",      "a colour at random"),
	ACT(VNG_ACT_COLOUR_MIX),
	FIXED("ALT",          "the palette under the pointer"),
	GAP,

	HEAD("panels"),
	ACT(VNG_ACT_PANEL_PROJECTS),
	ACT(VNG_ACT_PANEL_SHEETS),
	ACT(VNG_ACT_PANEL_COLOUR),
	ACT(VNG_ACT_PANEL_PALETTE),
	ACT(VNG_ACT_PANEL_THUMB),
	ACT(VNG_ACT_PANEL_ANIM),
	ACT(VNG_ACT_OVERLAY),
	GAP,

	HEAD("looking"),
	ACT(VNG_ACT_VIEW_HOME),
	FIXED("wheel",        "zoom where the pointer is"),
	FIXED("SPACE drag",   "move the sheet about"),
};

#define LAYOUT_LOT ((int)(sizeof layout / sizeof layout[0]))

/* ------------------------------------------------------------------ what a key is called */

/*
 * SDL knows every key's name in both directions, so there is no table of our own to keep in
 * step with a keyboard. What is added here is the shape a person may write: a bare key and
 * nothing else, so anything with a modifier word in it is REFUSED rather than half-accepted.
 *
 * Refusing matters more than it looks. `panel-colour = CTRL C` is a line somebody would write
 * expecting it to work, and the honest answers are to bind it or to say no - not to bind C
 * and let them find out by pressing it.
 */
static bool a_modifier (const char *word)
{
	static const char *const MOD[] = { "CTRL", "SHIFT", "ALT", "GUI", "CMD",
	                                   "CONTROL", "META", "SUPER", "WIN" };

	for (size_t i = 0; i < SDL_arraysize(MOD); i++)
		if (SDL_strcasecmp(word, MOD[i]) == 0) return true;
	return false;
}

static bool spell_to_key (const char *text, SDL_Keycode *out)
{
	if (!text || !text[0]) return false;
	if (a_modifier(text))  return false;

	/* One word. Two means a chord, and a chord is not a bare key. */
	for (const char *p = text; *p; p++)
		if (*p == ' ' || *p == '\t') return false;

	SDL_Keycode k = SDL_GetKeyFromName(text);
	if (k == SDLK_UNKNOWN) return false;

	*out = k;
	return true;
}

/* ----------------------------------------------------------------------- the desk's rows */

int keymap_lot (void) { return LAYOUT_LOT; }

const VNG_KEYROW *keymap_row (int i)
{
	static VNG_KEYROW out;

	/*
	 * ONE SPELLING BUFFER PER ROW, and it is worth the three hundred bytes.
	 *
	 * A single shared buffer works for the caller this was written for - the desk measures a
	 * row and then draws it, one at a time. It breaks the moment anybody holds two rows at
	 * once, and it breaks SILENTLY: both pointers are still valid, and both now read the same
	 * key. That is not a bug a caller can see; it just prints the wrong letter.
	 *
	 * Found by a check that asked for two rows in the same expression and got one answer
	 * twice. A buffer per row makes the returned pointer good until that row is asked for
	 * again, which is the promise a caller would reasonably assume anyway.
	 */
	static char spell[LAYOUT_LOT][16];

	if (i < 0 || i >= LAYOUT_LOT) return NULL;

	seed();

	const SHOWROW *r = &layout[i];

	if (r->head) { out.key = r->head; out.does = NULL;    return &out; }
	if (r->keys) { out.key = r->keys; out.does = r->does; return &out; }

	if (r->act < 0) { out.key = NULL; out.does = NULL; return &out; }

	const char *name = SDL_GetKeyName(live[r->act]);
	SDL_strlcpy(spell[i], (name && name[0]) ? name : "?", sizeof spell[i]);

	out.key  = spell[i];
	out.does = fallback[r->act].does;
	return &out;
}

/* -------------------------------------------------------------------------- the matching */

bool keymap_hit (VNG_ACT a, const SDL_Event *e)
{
	if (a < 0 || a >= VNG_ACT_LOT) return false;
	if (!e || e->type != SDL_EVENT_KEY_DOWN) return false;

	seed();

	if (e->key.repeat) return false;

	/* BARE means no modifier at all - see keymap.h on why the narrow question is the bug. */
	if (e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI))
		return false;

	return e->key.key == live[a];
}

/* ------------------------------------------------------------------------------ the file */

/*
 * THE STAMP. A file naming actions an older build did not have - or missing ones this build
 * needs - is a file that silently leaves half the program on its defaults while looking
 * complete. Raise this whenever an action is added, removed or renamed.
 */
#define KEYMAP_STAMP "# vangopix-keys 3"

static const char *const PREAMBLE =
	KEYMAP_STAMP "\n"
	"#\n"
	"# The keys, and the list the empty desk shows. Change the key on the right.\n"
	"#\n"
	"#   name = KEY    one binding. The name is what Vangopix dispatches on; the key is\n"
	"#                 spelled the way SDL names it - A, 7, F1, Escape, Tab, Space.\n"
	"#   [group]       a heading, for reading. Vangopix ignores these.\n"
	"#   # ...         a comment, like these.\n"
	"#\n"
	"# BARE KEYS ONLY. One key, no modifier. A line with CTRL or SHIFT in it is refused and\n"
	"# that action keeps its default - the shortcuts built out of modifiers, and the gestures\n"
	"# that are not presses at all, still live in the source. So do the descriptions: this\n"
	"# file cannot reword what a key does, only move it.\n"
	"#\n"
	"# TWO ACTIONS ON ONE KEY is refused too, and the second one keeps its default. Which of\n"
	"# them would have won is decided by the order the program asks, which is not visible\n"
	"# from here - so it is not offered as a choice.\n"
	"#\n"
	"# DELETE THIS FILE TO GET THE DEFAULTS BACK. Vangopix writes it again from its own list\n"
	"# the next time it starts. A line it cannot read is skipped and costs only that line.\n"
	"\n";

/* Does the group starting at this heading hold anything a person can bind? */
static bool group_binds (int at)
{
	for (int i = at + 1; i < LAYOUT_LOT; i++) {
		if (layout[i].head)     return false;
		if (layout[i].act >= 0) return true;
	}
	return false;
}

static bool write_default (const char *path)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "w");
	if (!io) return false;

	SDL_WriteIO(io, PREAMBLE, SDL_strlen(PREAMBLE));

	/*
	 * Walked in the DESK's order, not the enum's, so the file reads like the list a person has
	 * been looking at. Fixed rows write nothing - there is nothing to bind - and a heading
	 * whose whole group turned out to be fixed rows writes nothing either: `[a selection]` is
	 * four gestures and a chord, so a heading over nothing would be an invitation to add a
	 * line under it that the reader is then told is refused.
	 *
	 * `was_blank` keeps the gaps single once those two have been dropped. Without it every
	 * skipped group leaves its blank line behind and the file grows holes.
	 */
	bool was_blank = true;   /* the preamble already ends in one */

	for (int i = 0; i < LAYOUT_LOT; i++) {
		const SHOWROW *r = &layout[i];
		char line[160];
		int  n = 0;

		if (r->head) {
			if (!group_binds(i)) continue;
			n = SDL_snprintf(line, sizeof line, "[%s]\n", r->head);
		} else if (r->act >= 0) {
			const char *name = SDL_GetKeyName(live[r->act]);
			n = SDL_snprintf(line, sizeof line, "%-16s= %s\n",
			                 fallback[r->act].name, (name && name[0]) ? name : "?");
		} else if (!r->keys) {
			if (was_blank) continue;
			n = SDL_snprintf(line, sizeof line, "\n");
		}

		if (n <= 0) continue;

		SDL_WriteIO(io, line, (size_t)n);
		was_blank = (line[0] == '\n');
	}

	SDL_CloseIO(io);
	return true;
}

/* Takes the spaces off both ends, in place. */
static void trim (char *s)
{
	size_t a = 0;
	while (s[a] == ' ' || s[a] == '\t') a++;

	if (a) SDL_memmove(s, s + a, SDL_strlen(s + a) + 1);

	size_t n = SDL_strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}

static int act_named (const char *name)
{
	for (int i = 0; i < VNG_ACT_LOT; i++)
		if (SDL_strcasecmp(name, fallback[i].name) == 0) return i;
	return -1;
}

/* Who else is already on that key. -1 when nobody is. */
static int already_on (SDL_Keycode k, int except)
{
	for (int i = 0; i < VNG_ACT_LOT; i++)
		if (i != except && live[i] == k) return i;
	return -1;
}

static void read_file (const char *path)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "r");
	if (!io) return;

	char line[256];

	while (vangopix_read_line(io, line, sizeof line)) {
		trim(line);

		if (line[0] == 0 || line[0] == '#' || line[0] == '[') continue;

		char *eq = SDL_strchr(line, '=');
		if (!eq) { SDL_Log("keyboard.txt: not a binding: %s", line); continue; }

		*eq = 0;

		char *name  = line;
		char *spell = eq + 1;
		trim(name);
		trim(spell);

		int a = act_named(name);
		if (a < 0) { SDL_Log("keyboard.txt: no such action: %s", name); continue; }

		SDL_Keycode k;
		if (!spell_to_key(spell, &k)) {
			SDL_Log("keyboard.txt: %s = %s is not a bare key - keeping %s",
			        name, spell, SDL_GetKeyName(live[a]));
			continue;
		}

		/*
		 * TWO ACTIONS ON ONE KEY IS THE NEW WAY TO BE WRONG, and it is invisible from the
		 * file: which one wins is decided by the order the layers are asked, which is a fact
		 * about core.c. Refusing the second is the only answer that cannot surprise - and it
		 * says which line lost, because a binding that quietly did not take is exactly what
		 * a person would blame on their keyboard.
		 */
		int clash = already_on(k, a);
		if (clash >= 0) {
			SDL_Log("keyboard.txt: %s = %s is already %s - keeping %s",
			        name, spell, fallback[clash].name, SDL_GetKeyName(live[a]));
			continue;
		}

		live[a] = k;
	}

	SDL_CloseIO(io);
}

/* Does the file carry OUR stamp - see KEYMAP_STAMP. False for a missing file and false for
   one an older Vangopix wrote, which are the same answer: write it. */
static bool stamped (const char *path)
{
	size_t len = 0;
	char  *txt = (char *) SDL_LoadFile(path, &len);

	if (!txt) return false;

	bool ours = SDL_strstr(txt, KEYMAP_STAMP) != NULL;
	SDL_free(txt);
	return ours;
}

void keymap_load (void)
{
	/* The defaults go down FIRST and unconditionally, so an action the file forgets to
	 * mention is bound rather than dead - and so a second call cannot inherit the last
	 * one's edits. */
	seeded = false;
	seed();

	char *path = vangopix_asset(VNG_KEYMAP_FILE);
	if (!path) return;   /* the built-in map stands */

	/* Written before it is read, so the run that creates it uses the same map every later
	 * run will - and so a person who deletes it has it back without restarting twice. */
	if (!stamped(path)) write_default(path);

	read_file(path);
	SDL_free(path);
}
