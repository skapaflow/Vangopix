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
 *
 * UNSIZED, AND COUNTED BELOW. Sized by VNG_ACT_LOT, an action added to the enum without a row
 * here was a row of zeros - a NULL name that the first line of keyboard.txt handed to a string
 * compare. The count is checked where the compiler can refuse it.
 */
typedef struct {
	const char *name;
	const char *does;
	SDL_Keycode key;
} BIND;

static const BIND fallback[] = {
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
	{ "colour-blend",   "see-through colour: blend over",  SDLK_B },

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

SDL_COMPILE_TIME_ASSERT(keymap_has_a_row_per_action, SDL_arraysize(fallback) == VNG_ACT_LOT);

/*
 * Where each action ACTUALLY lives - the defaults until the file says otherwise, and
 * SDLK_UNKNOWN for an action whose default key a line of the file gave to another one.
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
	ACT(VNG_ACT_COLOUR_BLEND),
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

/* The name to write a key under, or "?" for one nobody holds or SDL cannot name. */
static const char *key_word (SDL_Keycode k)
{
	const char *name = (k != SDLK_UNKNOWN) ? SDL_GetKeyName(k) : NULL;
	return (name && name[0]) ? name : "?";
}

/* ----------------------------------------------------------------------- the desk's rows */

int keymap_lot (void) { return LAYOUT_LOT; }

const VNG_KEYROW *keymap_row (int i)
{
	/*
	 * ONE ROW STRUCT AND ONE SPELLING BUFFER PER ROW, and it is worth the few hundred bytes.
	 *
	 * A single shared answer works for the caller this was written for - the desk measures a
	 * row and then draws it, one at a time. It breaks the moment anybody holds two rows at
	 * once, and it breaks SILENTLY: both pointers are still valid, and both now read the same
	 * row. That is not a bug a caller can see; it just prints the wrong words.
	 *
	 * The spelling buffers were made per row after a check asked for two rows in the same
	 * expression and got one key twice - and the struct they were pointed from stayed shared,
	 * which is the same mistake one level up. Now the pointer handed back is good until THAT
	 * row is asked for again, which is the promise a caller would reasonably assume anyway.
	 */
	static VNG_KEYROW out[LAYOUT_LOT];
	static char       spell[LAYOUT_LOT][32];

	if (i < 0 || i >= LAYOUT_LOT) return NULL;

	seed();

	const SHOWROW *r = &layout[i];
	VNG_KEYROW    *o = &out[i];

	if (r->head) { o->key = r->head; o->does = NULL;    return o; }
	if (r->keys) { o->key = r->keys; o->does = r->does; return o; }

	if (r->act < 0) { o->key = NULL; o->does = NULL; return o; }

	SDL_strlcpy(spell[i], key_word(live[r->act]), sizeof spell[i]);

	o->key  = spell[i];
	o->does = fallback[r->act].does;
	return o;
}

/* -------------------------------------------------------------------------- the matching */

bool keymap_hit (VNG_ACT a, const SDL_Event *e)
{
	if (a < 0 || a >= VNG_ACT_LOT) return false;
	if (!e || e->type != SDL_EVENT_KEY_DOWN) return false;

	seed();

	if (e->key.repeat) return false;

	/* An action whose key a line of the file gave to another has none - and a key event SDL
	 * could not name arrives as SDLK_UNKNOWN too, which must not fire it. */
	if (live[a] == SDLK_UNKNOWN) return false;

	/* BARE means no modifier at all - see keymap.h on why the narrow question is the bug. */
	if (e->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI))
		return false;

	return e->key.key == live[a];
}

/* ------------------------------------------------------------------------------ the file */

/*
 * THE STAMP. A file naming actions an older build did not have - or missing ones this build
 * needs - is a file that silently leaves half the program on its defaults while looking
 * complete. Raise this whenever an action is added, removed or renamed: a file with an older
 * stamp is then read for the keys it still names, and written again with everything this
 * build has - see keymap_load in keymap.h.
 */
#define KEYMAP_STAMP "# vangopix-keys 4"

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
	"# TWO ACTIONS ON ONE KEY: the line further down is refused, and that action keeps its\n"
	"# default if nothing else holds it. Swapping two keys is two lines, and works. A key\n"
	"# written here beats a default: the action that only had it by default is left without\n"
	"# one, and the desk shows it as ?.\n"
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

/* Writes the map IN HAND - the defaults, and whatever an older file said that still holds. */
static bool write_map (const char *path)
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
			n = SDL_snprintf(line, sizeof line, "%-16s= %s\n",
			                 fallback[r->act].name, key_word(live[r->act]));
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

/* Who in `map` already holds that key. -1 when nobody does. */
static int holder (const SDL_Keycode *map, SDL_Keycode k)
{
	for (int i = 0; i < VNG_ACT_LOT; i++)
		if (map[i] == k) return i;
	return -1;
}

/*
 * THE FILE, READ IN TWO PASSES - and the second pass is the fix.
 *
 * Every line used to be checked, as it was read, against the map as it stood: the defaults,
 * plus whatever the lines above had changed. So SWAPPING two keys could not be written at all.
 * `tool-pencil = W` met the line tool still sitting on W by default and was refused, and
 * `tool-line = Q` then met the pencil still sitting on Q and was refused too - whichever order
 * the two lines came in. The file offers every key as editable; it has to mean it.
 *
 * So the lines are only collected here - the last word on each action wins, the way a later
 * line wins in any settings file - and settled afterwards, all at once:
 *
 *   1. the actions the file names claim their keys, in the order their lines were written;
 *      a key an earlier line already claimed refuses the later one;
 *   2. every action left without a claim - not named, or refused - gets its default back,
 *      if nobody claimed that. An action that only had a key BY DEFAULT loses it to a line
 *      that asked for it on purpose, and goes without one; the desk shows it as "?".
 *
 * Each refusal says which line lost and to what, because a binding that quietly did not take
 * is exactly what a person would blame on their keyboard.
 */
static void read_file (const char *path)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "r");
	if (!io) return;

	SDL_Keycode want[VNG_ACT_LOT];
	int         line_of[VNG_ACT_LOT];
	for (int i = 0; i < VNG_ACT_LOT; i++) { want[i] = SDLK_UNKNOWN; line_of[i] = 0; }

	char line[256];
	int  nth = 0;

	while (vangopix_read_line(io, line, sizeof line)) {
		nth++;
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
			        name, spell, key_word(live[a]));
			continue;
		}

		want[a]    = k;
		line_of[a] = nth;
	}

	SDL_CloseIO(io);

	/* 1. The named actions, in the order they were written. */
	int order[VNG_ACT_LOT], n = 0;
	for (int a = 0; a < VNG_ACT_LOT; a++)
		if (want[a] != SDLK_UNKNOWN) order[n++] = a;

	for (int i = 1; i < n; i++)                 /* twenty at most: an insertion sort */
		for (int j = i; j > 0 && line_of[order[j]] < line_of[order[j - 1]]; j--) {
			int s = order[j]; order[j] = order[j - 1]; order[j - 1] = s;
		}

	SDL_Keycode got[VNG_ACT_LOT];
	for (int a = 0; a < VNG_ACT_LOT; a++) got[a] = SDLK_UNKNOWN;

	for (int i = 0; i < n; i++) {
		int a = order[i];
		int b = holder(got, want[a]);

		if (b >= 0) {
			SDL_Log("keyboard.txt: %s = %s is already %s - keeping its default",
			        fallback[a].name, key_word(want[a]), fallback[b].name);
			continue;
		}
		got[a] = want[a];
	}

	/* 2. Everything left, back on its default if that is still free. */
	for (int a = 0; a < VNG_ACT_LOT; a++) {
		if (got[a] != SDLK_UNKNOWN) continue;

		int b = holder(got, fallback[a].key);
		if (b >= 0) {
			SDL_Log("keyboard.txt: %s has no key - %s is %s now",
			        fallback[a].name, key_word(fallback[a].key), fallback[b].name);
			continue;
		}
		got[a] = fallback[a].key;
	}

	SDL_memcpy(live, got, sizeof live);
}

/* Does the file carry OUR stamp - see KEYMAP_STAMP. */
static bool stamped (const char *path)
{
	size_t len = 0;
	char  *txt = (char *) SDL_LoadFile(path, &len);

	if (!txt) return false;

	bool ours = SDL_strstr(txt, KEYMAP_STAMP) != NULL;
	SDL_free(txt);
	return ours;
}

void keymap_load_from (const char *path)
{
	/* The defaults go down FIRST and unconditionally, so an action the file forgets to
	 * mention is bound rather than dead - and so a second call cannot inherit the last
	 * one's edits. */
	seeded = false;
	seed();

	if (!path) return;   /* the built-in map stands */

	/*
	 * READ BEFORE IT IS WRITTEN, WHATEVER THE STAMP SAYS - which is where this used to part
	 * company with config.txt. A file from an older build was written over from the defaults,
	 * so every key somebody had moved went back to where it was on the first run of the new
	 * build, without a word. It is read first now, and the rewrite below carries what it said
	 * forward: keys moved stay moved, and actions this build added arrive on their defaults.
	 *
	 * Written before the NEXT run reads it, so the run that creates it uses the same map every
	 * later run will - and a person who deletes it has it back without restarting twice.
	 */
	read_file(path);

	if (!stamped(path)) write_map(path);
}

void keymap_load (void)
{
	char *path = vangopix_asset(VNG_KEYMAP_FILE);
	keymap_load_from(path);
	SDL_free(path);
}
