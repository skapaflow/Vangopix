#include "style.h"
#include "tool.h"   /* tool_hex and tool_hex_read - the one writer and reader of a colour */

#include <stddef.h> /* offsetof */

/*
 * THE DEFAULTS, and they are the program as it looked before this file existed - every value
 * below is a literal that used to sit where it was drawn. 0xAARRGGBB, like everything in here.
 *
 * Stated ONCE and used twice: to start vng_style, so a program that never reads a file looks
 * right, and to put it back in style_reset. Two copies would be two defaults.
 */
#define DEFAULTS {                                                                         \
	.desk_a = 0xFF252525u, .desk_b = 0xFF303030u, .desk_square = 6,                        \
	.sheet_frame = 0xFF000000u,                                                             \
	                                                                                        \
	.font_size = 20.0f, .font_small = 14.0f,                                                \
	.text = 0xFFDCDCDCu, .text_dim = 0xFF909090u,                                           \
	.accent = 0xFFFF8000u, .highlight = 0xFF4C9AFFu,                                        \
	                                                                                        \
	.win_bg = 0xFF141414u, .win_bg_alpha = 0x80,                                            \
	.win_bar = 0xFF222222u, .win_title = 0xFFB4B4B4u,                                       \
	.win_border = 0xFF303030u, .win_grip = 0xFF606060u, .win_inset = 0xFF0C0C0Cu,           \
	                                                                                        \
	.panel = 0xFF141414u, .sidebar = 0xDD000000u,                                           \
	.tab = 0x80101010u, .tab_active = 0x40303030u,                                          \
	.close = 0xFFC03A3Au, .close_hot = 0xFFFF5A5Au,                                         \
	                                                                                        \
	.pal_cell = 24.0f, .pal_a = 0xFF303050u, .pal_b = 0xFF606080u,                          \
	                                                                                        \
	.where = 0xFFFFD800u, .span = 0xFF50C0FFu, .eraser_fill = 0x80FFFFFFu,                  \
	                                                                                        \
	.anim_bg = { 0xFF455212u, 0xFF000000u, 0xFFFFFFFFu, 0x00000000u }, .anim_bg_lot = 4     \
}

VNG_STYLE vng_style = DEFAULTS;

void style_reset (void)
{
	static const VNG_STYLE fresh = DEFAULTS;
	vng_style = fresh;
}

Uint32 style_rgba  (Uint32 argb)          { return (argb << 8) | (argb >> 24); }
Uint32 style_alpha (Uint32 argb, Uint8 a) { return (argb & 0x00FFFFFFu) | ((Uint32)a << 24); }

void style_ink (Uint32 argb)
{
	SDL_SetRenderDrawColor(vng_ren, (Uint8)((argb >> 16) & 0xFF), (Uint8)((argb >> 8) & 0xFF),
	                                (Uint8)( argb        & 0xFF), (Uint8)((argb >> 24) & 0xFF));
}

/* ----------------------------------------------------------------------------- the table */

/*
 * WHAT A SETTING IS: a name in the file, a field in VNG_STYLE, and what may go in it.
 *
 *   COLOUR  one colour
 *   PAIR    two colours, into two fields that sit side by side in the struct
 *   RGB     one colour whose alpha is not the theme's to set - a panel keeps its own opacity,
 *           and window_bg_color's is window_bg_alpha. Written as six digits so the file does
 *           not show an alpha that does nothing
 *   LIST    one colour or up to VNG_STYLE_BG_MAX, with a count beside them
 *   INT     a whole number into an int
 *   PX      a whole number of pixels into a float, which is what the layout measures in
 *
 * ONE TABLE, READ AND WRITTEN. The reader looks names up in it and the writer walks it in
 * order, so a setting cannot be readable and never written, or written under a name the
 * reader does not know - the drift keymap.c avoids the same way.
 */
typedef enum { K_COLOUR, K_PAIR, K_RGB, K_LIST, K_INT, K_PX } KIND;

typedef struct {
	const char *group;   /* non-NULL opens a [group] in the written file */
	const char *name;
	KIND        kind;
	size_t      at;      /* offsetof into VNG_STYLE */
	int         lo, hi;  /* INT and PX */
	int         step;    /* PX: rounded DOWN to a multiple of this, when it is not 0 */
	const char *note;    /* written above it; lines split on \n */
} ENTRY;

#define AT(f) offsetof(VNG_STYLE, f)

static const ENTRY table[] = {
	{ "desk", "background_color", K_PAIR, AT(desk_a), 0, 0, 0,
	  "the checkerboard behind the sheet - what shows through anything transparent" },
	{ NULL, "background_size", K_INT, AT(desk_square), 1, 64, 0,
	  "the side of one square of it, in screen pixels" },
	{ NULL, "sheet_frame_color", K_COLOUR, AT(sheet_frame), 0, 0, 0,
	  "the hairline just outside the sheet, so an image that is mostly alpha has an edge" },

	{ "text", "font_size", K_PX, AT(font_size), 8, 32, 0,
	  "the face, in pixels. Every box that holds text is measured from it, so the whole\n"
	  "interface grows and shrinks with this one number" },
	{ NULL, "font_small_size", K_PX, AT(font_small), 6, 32, 0,
	  "the small face: labels in boxes the main one would fill" },
	{ NULL, "text_color", K_COLOUR, AT(text), 0, 0, 0,
	  "text on a panel" },
	{ NULL, "text_dim_color", K_COLOUR, AT(text_dim), 0, 0, 0,
	  "text that is there but is not the one in use - a tab behind the current one" },
	{ NULL, "accent_color", K_COLOUR, AT(accent), 0, 0, 0,
	  "labels, and the rows of a list or a menu - what can be pressed" },
	{ NULL, "highlight_color", K_COLOUR, AT(highlight), 0, 0, 0,
	  "the tab in front, and the corner grips that resize the sheet" },

	{ "windows", "window_bg_color", K_RGB, AT(win_bg), 0, 0, 0,
	  "the ground behind a window's contents. Its opacity is the next line" },
	{ NULL, "window_bg_alpha", K_INT, AT(win_bg_alpha), 0, 255, 0,
	  "0 lets the drawing show through completely, 255 hides it" },
	{ NULL, "window_bar_color", K_COLOUR, AT(win_bar), 0, 0, 0,
	  "the bar a window is dragged by" },
	{ NULL, "window_title_color", K_COLOUR, AT(win_title), 0, 0, 0,
	  "the title written on it" },
	{ NULL, "window_border_color", K_COLOUR, AT(win_border), 0, 0, 0,
	  "the hairline round a window, and round the panels and readouts" },
	{ NULL, "window_grip_color", K_COLOUR, AT(win_grip), 0, 0, 0,
	  "the strokes in the corner a window is stretched by" },
	{ NULL, "window_inset_color", K_COLOUR, AT(win_inset), 0, 0, 0,
	  "a list, a menu or a text field inside a window" },

	{ "panels", "panel_color", K_RGB, AT(panel), 0, 0, 0,
	  "the size prompt, the readouts, the file preview and the strip on the empty desk.\n"
	  "Each keeps its own opacity" },
	{ NULL, "sidebar_color", K_COLOUR, AT(sidebar), 0, 0, 0,
	  "the project panel TAB brings up. It floats over the drawing, so its alpha is\n"
	  "how much of the drawing shows through" },
	{ NULL, "tab_color", K_COLOUR, AT(tab), 0, 0, 0,
	  "a tab in the row ESC brings up, and the [+] beside them" },
	{ NULL, "tab_active_color", K_COLOUR, AT(tab_active), 0, 0, 0,
	  "the tab in front. A line in highlight_color marks it as well, so it never\n"
	  "depends on telling two dark greys apart" },
	{ NULL, "close_color", K_COLOUR, AT(close), 0, 0, 0,
	  "the dot that closes a window, a tab, a project or a clip" },
	{ NULL, "close_hot_color", K_COLOUR, AT(close_hot), 0, 0, 0,
	  "the same, under the pointer" },

	{ "palette", "palette_size", K_PX, AT(pal_cell), 8, 64, 4,
	  "one swatch, in pixels - in the grid beside the palette and the one under ALT.\n"
	  "A multiple of four, so the board under it stays whole across the cells" },
	{ NULL, "palette_board_color", K_PAIR, AT(pal_a), 0, 0, 0,
	  "the board under an empty or transparent swatch - bluer than the desk on purpose,\n"
	  "so the grid can be told from the table it floats over" },

	{ "readouts", "position_color", K_COLOUR, AT(where), 0, 0, 0,
	  "(x,y) beside the pointer - where it is, whether anything is happening or not" },
	{ NULL, "size_color", K_COLOUR, AT(span), 0, 0, 0,
	  "[w x h] while a drag runs - how far it has gone" },
	{ NULL, "eraser_fill_color", K_COLOUR, AT(eraser_fill), 0, 0, 0,
	  "the wash over what the eraser is about to take, square or circle (SHIFT+TAB).\n"
	  "Half transparent by default, so what is under it still shows" },

	{ "animation", "animation_backgrounds", K_LIST, AT(anim_bg), 1, VNG_STYLE_BG_MAX, 0,
	  "behind the animation preview, one after another on the right button.\n"
	  "An alpha of 00 is no background at all" },
};

#define TABLE_LOT ((int)(sizeof table / sizeof table[0]))

static void *field (const ENTRY *e) { return (char *)&vng_style + e->at; }

/* ------------------------------------------------------------------------------ reading */

/* Takes the spaces off both ends, in place. */
static char *trim (char *s)
{
	while (*s == ' ' || *s == '\t') s++;

	size_t n = SDL_strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
	return s;
}

/* Colours separated by commas, into `out`. How many, or -1 when any of them is not a colour
 * or there are more than `cap` - a list half read is a list that says something nobody wrote. */
static int colours (char *text, Uint32 *out, int cap)
{
	int n = 0;

	for (char *p = text; ; ) {
		char *comma = SDL_strchr(p, ',');
		if (comma) *comma = 0;

		if (n == cap) return -1;
		if (!tool_hex_read(trim(p), &out[n])) return -1;
		n++;

		if (!comma) break;
		p = comma + 1;
	}
	return n;
}

static bool whole (const char *text, int *out)
{
	char *end = NULL;
	long  v   = SDL_strtol(text, &end, 10);

	if (end == text || *trim(end) != 0) return false;
	*out = (int)v;
	return true;
}

static const ENTRY *named (const char *name)
{
	for (int i = 0; i < TABLE_LOT; i++)
		if (SDL_strcasecmp(name, table[i].name) == 0) return &table[i];
	return NULL;
}

bool style_line (const char *raw)
{
	char buf[512];
	SDL_strlcpy(buf, raw ? raw : "", sizeof buf);

	char *line = trim(buf);
	if (line[0] == 0 || line[0] == '#' || line[0] == '[') return false;

	/* `name: value` is what the file is written in; `name = value` is keyboard.txt's shape and
	 * a hand used to one will type the other. Whichever comes FIRST splits the line, since a
	 * value never holds either. */
	char *colon = SDL_strchr(line, ':'), *eq = SDL_strchr(line, '=');
	char *sep   = (colon && (!eq || colon < eq)) ? colon : eq;
	if (!sep) { SDL_Log("config.txt: not a setting: %s", line); return false; }

	*sep = 0;
	char *name  = trim(line);
	char *value = trim(sep + 1);

	const ENTRY *e = named(name);
	if (!e) { SDL_Log("config.txt: no such setting: %s", name); return false; }

	/* What was written, kept whole for the log: reading a list cuts the value up at its
	 * commas, and a refusal that quoted only the first colour would point at the wrong one. */
	char said[512];
	SDL_strlcpy(said, value, sizeof said);

	switch (e->kind) {

	case K_COLOUR:
	case K_RGB: {
		Uint32 c;
		if (colours(value, &c, 1) != 1) break;
		if (e->kind == K_RGB) c |= 0xFF000000u;
		*(Uint32 *)field(e) = c;
		return true;
	}

	case K_PAIR: {
		Uint32 c[2];
		if (colours(value, c, 2) != 2) break;
		((Uint32 *)field(e))[0] = c[0];
		((Uint32 *)field(e))[1] = c[1];
		return true;
	}

	case K_LIST: {
		Uint32 c[VNG_STYLE_BG_MAX];
		int    n = colours(value, c, e->hi);
		if (n < e->lo) break;
		SDL_memcpy(field(e), c, (size_t)n * sizeof *c);
		vng_style.anim_bg_lot = n;   /* the one list there is */
		return true;
	}

	case K_INT:
	case K_PX: {
		int v;
		if (!whole(value, &v)) break;

		/* OUT OF RANGE IS PULLED IN, not refused. A person who asks for a font of 60 wants
		 * a big font, and the biggest one there is answers that better than the default
		 * would - where a value that is not a number at all has said nothing to go on. */
		if (v < e->lo) v = e->lo;
		if (v > e->hi) v = e->hi;
		if (e->step > 0) v -= v % e->step;

		if (e->kind == K_INT) *(int   *)field(e) = v;
		else                  *(float *)field(e) = (float)v;
		return true;
	}
	}

	SDL_Log("config.txt: %s: '%s' is not what it takes - keeping the one it had", name, said);
	return false;
}

/* ------------------------------------------------------------------------------ writing */

/*
 * THE STAMP. Raise it whenever a setting is added, removed or renamed: a file from before the
 * change is then read for what it still names and written again with everything this build
 * has - see style_load in style.h for why that keeps the values instead of resetting them.
 */
#define STYLE_STAMP "# vangopix-config 2"   /* 2: eraser_fill_color */

static const char *const PREAMBLE =
	STYLE_STAMP "\n"
	"#\n"
	"# How Vangopix looks. Change the value on the right, then start Vangopix again.\n"
	"#\n"
	"#   name: value    one setting. `name = value` is read as well.\n"
	"#   [group]        a heading, for reading. Vangopix ignores these.\n"
	"#   # ...          a comment, like these.\n"
	"#\n"
	"# COLOURS ARE 0xRRGGBBAA - red, green, blue, then alpha, the order the colour window\n"
	"# shows and other editors paste. `0x` and `#` are both optional; six digits is opaque and\n"
	"# three is the shorthand. Where two or more are asked for, separate them with commas.\n"
	"#\n"
	"# A NUMBER OUT OF RANGE is pulled in to the nearest one allowed. A line Vangopix cannot\n"
	"# read is skipped, costs only that line, and says why in the log.\n"
	"#\n"
	"# DELETE THIS FILE TO GET THE DEFAULTS BACK. It is written again the next time Vangopix\n"
	"# starts.\n";

static void value_of (const ENTRY *e, char *dst, size_t cap)
{
	char a[16], b[16];

	switch (e->kind) {
	case K_COLOUR:
		tool_hex(*(Uint32 *)field(e), a, sizeof a);
		SDL_snprintf(dst, cap, "0x%s", a);
		break;

	case K_RGB:
		SDL_snprintf(dst, cap, "0x%06X", (unsigned)(*(Uint32 *)field(e) & 0x00FFFFFFu));
		break;

	case K_PAIR:
		tool_hex(((Uint32 *)field(e))[0], a, sizeof a);
		tool_hex(((Uint32 *)field(e))[1], b, sizeof b);
		SDL_snprintf(dst, cap, "0x%s,0x%s", a, b);
		break;

	case K_LIST: {
		size_t used = 0;
		dst[0] = 0;
		for (int i = 0; i < vng_style.anim_bg_lot && used < cap; i++) {
			tool_hex(((Uint32 *)field(e))[i], a, sizeof a);
			used += (size_t)SDL_snprintf(dst + used, cap - used, "%s0x%s", i ? "," : "", a);
		}
		break;
	}

	case K_INT: SDL_snprintf(dst, cap, "%d", *(int *)field(e));          break;
	case K_PX:  SDL_snprintf(dst, cap, "%d", (int)*(float *)field(e));   break;
	}
}

static bool write_file (const char *path)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "w");
	if (!io) {
		SDL_Log("config.txt: cannot write %s: %s", path, SDL_GetError());
		return false;
	}

	SDL_WriteIO(io, PREAMBLE, SDL_strlen(PREAMBLE));

	for (int i = 0; i < TABLE_LOT; i++) {
		const ENTRY *e = &table[i];
		char line[256];
		int  n;

		if (e->group) {
			n = SDL_snprintf(line, sizeof line, "\n[%s]\n", e->group);
			SDL_WriteIO(io, line, (size_t)n);
		}

		/* The note, a comment line per \n in it. */
		for (const char *p = e->note; p && *p; ) {
			const char *nl = SDL_strchr(p, '\n');
			size_t      k  = nl ? (size_t)(nl - p) : SDL_strlen(p);

			SDL_WriteIO(io, "# ", 2);
			SDL_WriteIO(io, p, k);
			SDL_WriteIO(io, "\n", 1);
			p += k + (nl ? 1 : 0);
		}

		char v[160];
		value_of(e, v, sizeof v);
		n = SDL_snprintf(line, sizeof line, "%s: %s\n", e->name, v);
		SDL_WriteIO(io, line, (size_t)n);
	}

	SDL_CloseIO(io);
	return true;
}

/* ------------------------------------------------------------------------------ the file */

/* Does it carry OUR stamp. False for a missing file and for one an older build wrote, which
   are the same answer: write it. */
static bool stamped (const char *path)
{
	size_t len = 0;
	char  *txt = (char *) SDL_LoadFile(path, &len);
	if (!txt) return false;

	bool ours = SDL_strstr(txt, STYLE_STAMP) != NULL;
	SDL_free(txt);
	return ours;
}

void style_load_from (const char *path)
{
	/* The defaults FIRST and unconditionally, so a setting the file forgets is set rather
	 * than left over from a previous call. */
	style_reset();
	if (!path) return;

	bool ours = stamped(path);

	/* READ BEFORE WRITING, whatever the stamp says: a file from an older build still holds
	 * the values somebody chose, and the rewrite below is what carries them forward. */
	SDL_IOStream *io = SDL_IOFromFile(path, "r");
	if (io) {
		char line[512];
		while (vangopix_read_line(io, line, sizeof line)) style_line(line);
		SDL_CloseIO(io);
	}

	/* The small face is the small one. Asked for bigger than the main face, it would be a
	 * second main face that the boxes sized for a label do not have room for. */
	if (vng_style.font_small > vng_style.font_size) vng_style.font_small = vng_style.font_size;

	if (!ours) write_file(path);
}

void style_load (void)
{
	char *path = vangopix_asset(VNG_STYLE_FILE);
	style_load_from(path);
	SDL_free(path);
}
