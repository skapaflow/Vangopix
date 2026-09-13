#ifndef VANGOPIX_STYLE_H
#define VANGOPIX_STYLE_H

#include "vangopix.h"

#define VNG_STYLE_FILE "config.txt"

/*
 * HOW THE PROGRAM LOOKS, AND THE FILE THAT SAYS SO.
 *
 * keyboard.txt moved the keys out of the source; config.txt does the same for the colours and
 * the sizes. Beside the executable, written from the defaults when it is not there, a version
 * on its first line - the same contract, for the same reasons, read the same way.
 *
 * ---------------------------------------------------------------------------------------
 *
 * ONE PLACE ANSWERS "WHAT COLOUR IS THIS", AND THE MODULES ASK IT. The numbers used to be
 * literals where they were drawn - ninety-odd of them across eighteen files - and several were
 * the SAME decision written out again in each: the orange every list row and label is printed
 * in, the grey every panel sits on, the blue that says which tab is current. A file that could
 * move one copy of a decision and not the others would be a file that makes the program
 * disagree with itself. So a setting here is a ROLE rather than a spot - `accent_color` is
 * every place that orange means "this is a thing you can press" - and each module reads the
 * role instead of carrying its own literal.
 *
 * WHAT IS NOT HERE IS DELIBERATE, and the line is the one ui.h already draws. A colour that
 * carries MEANING stays in the source: the hue ring is hue, the first tool colour starts black
 * and the second starts as nothing because that is what makes the right button an eraser, a
 * red rim on the animation preview says "this frame is off the sheet", the white and black of
 * the marching rectangle and the pixel outline are there to read against ANY artwork. Those
 * are behaviour that happens to be a colour, and a theme that could change them could break
 * them.
 *
 * ---------------------------------------------------------------------------------------
 *
 * COLOURS ARE 0xRRGGBBAA IN THE FILE AND 0xAARRGGBB IN HERE, which is the trap this program
 * has fallen into twice and the reason both orders are named at the top of every place that
 * handles one. The file uses the shape the colour window shows and another editor pastes, so
 * a colour copied from anywhere drops straight in; everything below is converted once, on the
 * way in, to the order the document and the primitives use. text_print and glyph_draw still
 * take 0xRRGGBBAA, and style_rgba is the one named way across.
 *
 * READ ONCE, WHEN THE PROGRAM STARTS. The font sizes decide how big the atlases are packed and
 * the palette's cell decides the geometry both of its grids share, so a value changed under a
 * running frame would have to rebuild half the program in the middle of it. Restarting is the
 * honest version of that.
 */

#define VNG_STYLE_BG_MAX  8    /* the animation preview's backgrounds, at most */

typedef struct {
	/* [desk] */
	Uint32 desk_a, desk_b;     /* background_color: the checkerboard behind the sheet */
	int    desk_square;        /* background_size */
	Uint32 sheet_frame;        /* sheet_frame_color: the hairline just outside the sheet */

	/* [text] */
	float  font_size;          /* font_size */
	float  font_small;         /* font_small_size */
	Uint32 text;               /* text_color */
	Uint32 text_dim;           /* text_dim_color */
	Uint32 accent;             /* accent_color: labels, list rows - what can be pressed */
	Uint32 highlight;          /* highlight_color: the current tab, the corner grips */

	/* [windows] */
	Uint32 win_bg;             /* window_bg_color - its own alpha is window_bg_alpha's */
	int    win_bg_alpha;       /* window_bg_alpha, 0..255 */
	Uint32 win_bar;            /* window_bar_color */
	Uint32 win_title;          /* window_title_color */
	Uint32 win_border;         /* window_border_color */
	Uint32 win_grip;           /* window_grip_color */
	Uint32 win_inset;          /* window_inset_color: a list, a menu, a text field */

	/* [panels] */
	Uint32 panel;              /* panel_color - each panel keeps its own opacity */
	Uint32 sidebar;            /* sidebar_color: the project panel's ground, alpha and all */
	Uint32 tab;                /* tab_color: a tab that is not in front, and the [+] */
	Uint32 tab_active;         /* tab_active_color */
	Uint32 close, close_hot;   /* close_color, close_hot_color */

	/* [palette] */
	float  pal_cell;           /* palette_size - a multiple of four, see style.c */
	Uint32 pal_a, pal_b;       /* palette_board_color */

	/* [readouts] */
	Uint32 where;              /* position_color: (x,y) beside the pointer */
	Uint32 span;               /* size_color: [w x h] while a drag runs */
	Uint32 eraser_fill;        /* eraser_fill_color: the wash over what the eraser will take */

	/* [animation] */
	Uint32 anim_bg[VNG_STYLE_BG_MAX];   /* animation_backgrounds, cycled by the right button */
	int    anim_bg_lot;
} VNG_STYLE;

/*
 * THE STYLE IN USE. It starts as the built-in defaults, before anything is read - so a check
 * that never loads a file, and a program whose config.txt cannot be opened, both look exactly
 * as the source says. keymap.c learned why that matters: a table that is empty until somebody
 * remembers to fill it is a program that quietly draws nothing.
 */
extern VNG_STYLE vng_style;

/*
 * Reads config.txt beside the executable, writing it first when it is not there.
 *
 * A FILE FROM AN OLDER BUILD IS READ AND THEN WRITTEN AGAIN, which is where this parts company
 * with keyboard.txt: that one is rewritten from the defaults and an edited key goes back to
 * where it was. Here every value the file still names is kept and the new settings join it,
 * so a person who spent an evening on their colours does not lose them to an upgrade.
 */
extern void style_load (void);

/* The same, on any path - which is what lets the checks exercise the file without touching
   the one beside the executable, where checks.exe also lives. */
extern void style_load_from (const char *path);

/* Back to the built-in values. */
extern void style_reset (void);

/*
 * ONE LINE OF THE FILE, applied. False when it is not a setting - a comment, a heading, a
 * blank - or when it could not be read, in which case the setting keeps what it had and the
 * log says which line and why. A mistake costs the line and nothing else.
 */
extern bool style_line (const char *line);

/* 0xAARRGGBB to the 0xRRGGBBAA text_print and glyph_draw take. The one named way across. */
extern Uint32 style_rgba (Uint32 argb);

/* The colour with its alpha replaced - for a panel that takes its tone from here and keeps its
   own opacity, which was part of its design rather than part of the theme. */
extern Uint32 style_alpha (Uint32 argb, Uint8 a);

/* The renderer's draw colour, from 0xAARRGGBB - for the places that draw with SDL's own rect
   calls rather than the primitives, so they need not take a colour apart by hand each time. */
extern void style_ink (Uint32 argb);

#endif
