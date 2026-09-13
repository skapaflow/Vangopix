#ifndef VANGOPIX_PALETTE_H
#define VANGOPIX_PALETTE_H

#include "vangopix.h"
#include "tabs.h"
#include "tool.h"   /* VNG_CURSOR - the grid asks for a resize shape on its bands */

/*
 * THE PALETTE: ONE LIST OF COLOURS, SHOWN TWO WAYS.
 *
 * The first Vangopix had both and they were joined by one line nobody would notice:
 * core.c called `gui_quickly_palette_box(plt_pos, 20)` with plt_pos - THE SAME RECTANGLE
 * the palette window's grid had been stretched to. Resize the grid in the window, and the
 * grid that ALT summons under your hand changed shape with it.
 *
 * That is kept, and it is the reason these two live in one file: they are not two panels
 * that happen to draw swatches, they are ONE GRID SUMMONED TWO WAYS. The tiles - how many
 * across, how many down, how big, what a cell looks like, how an index becomes a position -
 * are stated once here and read by both. Splitting them would put that shared geometry in a
 * third place and give it two callers who could drift.
 *
 * WHAT EACH ONE IS FOR:
 *
 *   P holds the window.  A place to look at the whole set, stretch it, add to it, take
 *                        from it. Parked and stays, like the 1:1 panel.
 *   ALT holds the grid.  The set under your hand for as long as the key is down, and gone.
 *                        No window, no clicking, no putting away: hold, sweep, release.
 *
 * THE ALT GRID PICKS ON HOVER, AND IT IS THE ONE PLACE THAT IS RIGHT.
 *
 * "THE PICK IS ON THE PRESS, NOT ON HOVER" is settled for the sheet, and for the reason
 * recorded there: the hand rests, drifts and travels across the drawing on its way somewhere,
 * so a colour that changes under all of that is a colour nobody chose. NONE OF THAT IS TRUE
 * HERE. This grid exists only while a key is held, it is not somewhere a hand travels
 * through, and every cell in it is a colour - there is nothing to arrive at by accident. The
 * gesture is one movement: hold ALT, sweep to the colour, let go.
 *
 * And it is a proposal until the key comes up: sliding off the grid puts back the colour that
 * was in hand when ALT went down, so the whole gesture can be abandoned by moving away.
 * The first Vangopix meant to do this and tested only its FIRST CELL for it, so leaving the
 * grid from anywhere else left the last colour swept over. Here it tests the grid.
 *
 * A PRESS STILL SAYS WHICH SLOT, because that is the rule the rest of the program runs on:
 * left fills colour 1, right fills colour 2. Hover alone fills colour 1, which is what a
 * gesture with no button can mean. The original had no press at all and no way to load the
 * second colour from a palette.
 *
 * WHERE THE COLOURS COME FROM. The palette is scanned off the DOCUMENT - the unique colours
 * in the sheet, in the order they are met - which is what made the original's palette
 * non-empty in practice (`__load_palette_img__`, called on load). It is per tab, because a
 * palette is the working set of one drawing; it is freed with its tab, like the selection.
 *
 * Colours are also put in by hand, and they have to be: the colour window makes colours that
 * are not on the sheet yet, and without a way to keep one the palette could never hold it.
 * The original's ADD button lived inside the colour wheel, which this program deliberately
 * refuses to make into a palette panel - so it lives here, where the palette is.
 *
 * AND THE THIRD THING IS THE LIST. `COLOR` inside the box opens gui_list_palette's window -
 * the palettes of other machines, read out of vangopix_palette.ini. That file is the point of
 * the button: NINTENDO, GAMEBOY, MEGA-DRIVE, APPLE-II, MSX, MACINTOSH-II, PAINT-98 are sets
 * you cannot arrive at by eyedropping your own drawing, and they are what a person reaches
 * for when the whole job is "make this look like it came off that machine".
 */

/* How many colours a palette holds. 48 x 32 is the first Vangopix's own limit, and it is
   not arbitrary: it is the largest grid its window could show, so a colour past it was one
   nothing could ever reach. */
#define VNG_PAL_MAX  (48 * 32)

/*
 * One swatch, in screen pixels - the same in the grid beside the box and in the grid under
 * ALT, because they are one grid. The original's gd was 20; it is 24 now, at the author's
 * asking, so a colour is a bigger thing to hit and to see.
 *
 * Here and not private to palette.c because the checks stretch the grid a whole number of
 * cells and measure what they get; a test carrying its own 20 is a second copy of the layout,
 * and it is exactly the copy that went stale when this changed.
 */
#define VNG_PAL_CELL  24.0f

/* VNG_PAL is declared in tabs.h, beside the undo stack and the selection, because that is
   where it is stored - and a second typedef of it here would not be legal C99. */

extern void palette_free (VNG_PAL *p);   /* what vng_tab_close calls */

/* The list. `at` reads out of range as nothing rather than off the end. */
extern int    palette_lot (VNG_TAB *t);
extern Uint32 palette_at  (VNG_TAB *t, int i);

/* True when the colour was new. A repeat is not an error and not an insert - the original
   returned true for "already there" and false for "added", which reads backwards. */
extern bool palette_add (VNG_TAB *t, Uint32 argb);
extern void palette_del (VNG_TAB *t, Uint32 argb);
extern bool palette_has (VNG_TAB *t, Uint32 argb);

/*
 * Rebuilds the list from the document: every colour that is actually on the sheet, once
 * each, in the order the scan meets them. Fully transparent pixels are not colours and are
 * skipped, which is what the original did too.
 *
 * IT IS NOT THE ORIGINAL'S LOOP. That compared each pixel against every colour found so far,
 * which is fine for a 64x64 sprite and is twenty-four million pixels times fifteen hundred
 * comparisons on a photograph - and this program opens photographs. A hash set makes it one
 * pass.
 */
extern void palette_scan (VNG_TAB *t);

/*
 * The same, from any run of pixels rather than the whole sheet - what the selection's menu
 * hands it. Same rules as the scan, because it is the scan: once each, in the order met, and
 * nothing is not a colour.
 *
 *   keep false   CREATE PALETTE. The list is REPLACED, as loading a machine's set replaces
 *                it: the person is saying "these are the colours I am working in", and the
 *                ones not in that part of the drawing are exactly the ones being left out.
 *   keep true    ADD COLOR. The new ones go on the end, and a colour already there is not
 *                listed twice.
 */
extern void palette_from (VNG_TAB *t, const Uint32 *px, int n, bool keep);

/* ---------------------------------------------- the palettes of other machines, on COLOR */

/*
 * vangopix_palette.ini, beside the executable, one palette per line:
 *
 *     NINTENDO {#000000#fcfcfc#f8f8f8#bcbcbc...}
 *
 * It is the author's own file carried over from the first Vangopix - NINTENDO, GAMEBOY,
 * MEGA-DRIVE, APPLE-II, MSX, MACINTOSH-II, PAINT-98 - which is the whole point of the COLOR
 * button: a machine's fixed set is the thing you cannot eyedrop off your own drawing.
 *
 * Loading one REPLACES the tab's palette rather than adding to it, which is what the original
 * did and what choosing from a list means: you are saying which set you are working in.
 */
extern int         palette_list_lot  (void);
extern const char *palette_list_name (int i);
extern bool        palette_load      (VNG_TAB *t, const char *name);

/* ------------------------------------------------------------------- the window, on P */

/*
 * The box is 64 x 64 and does not stretch: two colour discs and the word COLOR, which is all
 * gui_palette_box ever held. Putting it away puts the list away with it - the list is a thing
 * the box opened, not a window of its own standing.
 */
extern void palette_toggle       (void);
extern bool palette_visible      (void);
extern bool palette_list_visible (void);

/* ------------------------------------------------- the grid of swatches, beside the box */

/*
 * THE GRID IS NOT IN THE WINDOW AND IT IS NOT A WINDOW. It hangs four pixels off the box's
 * outer right edge, level with its interior, and it is stretched by its own three 8px bands -
 * the right edge, the bottom edge and the corner where they meet. All of that is the
 * original's; a grid folded inside the frame would be a window that grows to hold a hundred
 * colours, which is a docked panel with a title bar on it.
 *
 * Because it is not a window, win.c neither routes nor clips it: it is offered the event one
 * rung UNDER the windows, after win_event has refused it, and drawn just before win_draw. A
 * window parked on top therefore covers it and gets the click, which is what a parked window
 * does to everything else.
 */
/*
 * Which resize shape the pointer should take at a point, or VNG_CUR_ARROW where the grid wants
 * none - which is NOT the same as asking for an arrow, since over the sheet the tool has
 * already asked for a crosshair.
 *
 * It takes a point rather than reading the mouse so the three stretch bands can be checked
 * without a hand on one.
 */
extern VNG_CURSOR palette_grid_cursor (float x, float y);

extern SDL_FRect palette_grid_area  (void);
extern bool      palette_grid_event (const SDL_Event *e, VNG_TAB *t);
extern void      palette_grid_draw  (VNG_TAB *t);

/* ------------------------------------------------------- the grid under the hand, on ALT */

/*
 * Offered every event, ABOVE the floating windows and below the two panels - because while
 * ALT is down this is the topmost thing on the screen, and it is drawn that way too.
 *
 * It consumes only what lands ON the grid: a press elsewhere with ALT held is not a gesture
 * this owns, and swallowing it would be swallowing a stroke.
 */
extern bool palette_quick_event (const SDL_Event *e, VNG_TAB *t);

/*
 * Where the grid is on screen right now, or a zero rectangle when it is not up.
 *
 * Exported for the reason win_area is: a check cannot predict where something summoned to the
 * pointer landed, and measuring from the real rectangle is what stops a test carrying a second
 * copy of the layout that can drift from the first.
 */
extern SDL_FRect palette_quick_area (void);

/*
 * The colour the grid would hand back for a point: the one under it, or - anywhere off the
 * grid - the one that was in hand when ALT went down, which is what makes the whole gesture
 * abandonable by moving away.
 *
 * Takes the point rather than reading the mouse so that exactly this can be checked, since it
 * is what the original got wrong: it tested its FIRST CELL, so the gesture could only be given
 * back by leaving through the top left corner.
 */
extern Uint32 palette_quick_hover (VNG_TAB *t, float x, float y);

/* Drawn last of everything, and it also does the hovering - a hover has no event of its own
   to hang on, which is why the original picked from inside its draw as well. */
extern void palette_quick_draw (VNG_TAB *t);

#endif
