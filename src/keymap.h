#ifndef VANGOPIX_KEYMAP_H
#define VANGOPIX_KEYMAP_H

#include "vangopix.h"

#define VNG_KEYMAP_FILE "keyboard.txt"

/*
 * WHAT THE KEYS ARE, THE FILE THAT SAYS SO, AND THE ONE QUESTION EVERY LAYER ASKS.
 *
 * This program has no menus and no toolbar, on purpose, so there is nowhere a person can go
 * and READ what it does. That is affordable right up until they arrive at it - which is why
 * the empty desk is where this list is written, and why F1 puts it back.
 *
 * ---------------------------------------------------------------------------------------
 *
 * THE FILE HOLDS ACTION-TO-KEY, AND NOT THE DESCRIPTION. That inversion is the whole design.
 *
 * It used to hold the text: `CTRL N = a new sheet`. A file shaped that way can only be a
 * caption, and a caption can LIE - edit the words and the program goes on doing what it
 * always did, describing itself confidently and wrongly. Every version of that idea needs a
 * warning printed in the file telling people not to believe it.
 *
 * Now it holds `panel-colour = C`: a NAME the program dispatches on, and the key it lives on.
 * The description belongs to the name and is built in, so the desk writes each line by
 * joining the key from the file to the words from the table - the same table the events are
 * matched against. The two cannot disagree, and the warning is gone because there is nothing
 * left to warn about.
 *
 * ---------------------------------------------------------------------------------------
 *
 * BARE KEYS ONLY, AND THAT IS A LINE WORTH BEING EXPLICIT ABOUT.
 *
 * A binding is not a string. It is a keycode, a modifier mask, and WHICH LAYER claims it -
 * and the layers disagree on purpose: bare S is the bucket, CTRL+S saves, and bare V flips a
 * selection only while there is one to flip. What is remappable here is the first kind: one
 * key, no modifier, one meaning. Everything with CTRL or SHIFT in it is still in the source,
 * and so are the gestures that are not presses at all - ALT held, SHIFT plus the wheel,
 * SPACE plus a drag. They are shown on the desk and marked as fixed rather than quietly
 * offered.
 *
 * The consequence to know: remapping the bare key does not move its modified relatives. Put
 * the project panel on G and SHIFT+TAB is still the change-colours limiter, because that is a
 * different binding that happens to share a key today.
 *
 * ---------------------------------------------------------------------------------------
 *
 * THE FILE IS WRITTEN WHEN IT IS NOT THERE, so deleting keyboard.txt is how a person gets the
 * defaults back - nothing to reinstall and nothing to remember. Its first line is a version;
 * a file stamped by an older Vangopix is written again, because a keymap naming actions that
 * no longer exist is a keymap that silently stops binding half of itself.
 *
 * A LINE IT CANNOT READ IS SKIPPED AND THAT ACTION KEEPS ITS DEFAULT - the rule the .anime
 * parser follows, for the same reason: this is a text file a person is invited to edit, so it
 * will contain mistakes, and a mistake must cost the line and nothing else.
 */

/*
 * THE ACTIONS. The name in the file is beside each one in keymap.c.
 *
 * THE NINE TOOLS ARE IN TOOL ORDER AND CONTIGUOUS, so a tool indexes its own action with no
 * table in between - the same trick glyph.h uses for the same reason. Nothing may be inserted
 * among them.
 */
typedef enum {
	VNG_ACT_TOOL_FIRST = 0,
	VNG_ACT_TOOL_PENCIL = VNG_ACT_TOOL_FIRST,
	VNG_ACT_TOOL_LINE,
	VNG_ACT_TOOL_RECT,
	VNG_ACT_TOOL_ELLIPSE,
	VNG_ACT_TOOL_ERASER,
	VNG_ACT_TOOL_BUCKET,
	VNG_ACT_TOOL_SPRAY,
	VNG_ACT_TOOL_CHANGE,
	VNG_ACT_TOOL_SELECT,

	VNG_ACT_COLOUR_MIX,

	VNG_ACT_SHEET_PREV,
	VNG_ACT_SHEET_NEXT,
	VNG_ACT_VIEW_HOME,

	VNG_ACT_PANEL_PROJECTS,
	VNG_ACT_PANEL_SHEETS,
	VNG_ACT_PANEL_COLOUR,
	VNG_ACT_PANEL_PALETTE,
	VNG_ACT_PANEL_THUMB,
	VNG_ACT_PANEL_ANIM,
	VNG_ACT_OVERLAY,

	VNG_ACT_LOT
} VNG_ACT;

/*
 * IS THIS EVENT THAT ACTION - the one question the layers ask, replacing every hand-written
 * `key == SDLK_X && no modifiers` in the program.
 *
 * It answers about the KEY and nothing else. Whether the layer is entitled to the event is
 * still the layer's own business: select.c asks this about a flip and then asks itself
 * whether there is a selection, and the answer to the second question is why bare V can mean
 * two things without either meaning being a special case here.
 *
 * BARE means no modifier at all, not "not the one that collides today". Asking it the narrow
 * way is how every later collision gets built in - it is the bug that had SHIFT+TAB raising
 * the project panel, and it is answered once, here.
 *
 * A key repeat is not a press. Nothing that maps to an action wants to fire while a key is
 * held down, and the two callers were already filtering repeats separately.
 */
extern bool keymap_hit (VNG_ACT a, const SDL_Event *e);

/*
 * Reads the file beside the executable, writing it first from the defaults when it is not
 * there or when it was written by an older build. It always leaves a usable map: a file that
 * cannot be read or cannot be written costs the file and nothing else.
 */
extern void keymap_load (void);

/* ------------------------------------------------------------- what the desk is shown */

typedef struct {
	const char *key;    /* NULL for a blank line */
	const char *does;   /* NULL for a heading - `key` is then the heading */
} VNG_KEYROW;

extern int               keymap_lot (void);
extern const VNG_KEYROW *keymap_row (int i);   /* NULL when i is off the end */

#endif
