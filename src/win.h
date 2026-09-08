#ifndef VANGOPIX_WIN_H
#define VANGOPIX_WIN_H

#include "vangopix.h"

/*
 * A WINDOW FRAME, AND DELIBERATELY NOT A WIDGET TOOLKIT.
 *
 * Section 7 said to leave the first Vangopix's src/window/ behind because VagrantUI would
 * replace it. It could not - VagrantUI emits VUI_CMD_RECT and VUI_CMD_TEXT and nothing else,
 * and the windows this program wants are mostly viewports onto pixels - so this was written
 * instead. It has since carried a window with a hue ring, four generated sliders and a text
 * field that owns the keyboard, and has needed nothing from anywhere else. Section 8 closes
 * the question: there is no bridge coming, and what a window needs next it gets here.
 *
 * WHAT IS TAKEN FROM winmgr AND WHAT IS NOT. Taken: the callback per window, which is
 * already the house pattern - tabbar.c, sidebar.c and thumb.c each draw their own pixels;
 * the z-order list with raise-on-press; drag by the head, stretch by the corner, a close box;
 * and the rule that a window holding the pointer stops the event there.
 *
 * Left behind: `warea[MGR16]`, sixteen sub-areas per window each with its own scrollbar -
 * that is a layout system, and it is where the complexity lived. Here the interior is ONE
 * rectangle and its owner divides it if it wants to. Left behind too: name[256] plus
 * reg_id[256] plus a hash to find a window by string, when the pointer is the identity
 * everywhere else in this program; and three globals for one list.
 *
 * THE INTERIOR IS CLIPPED FOR THE OWNER, so a window cannot spill onto the sheet by accident
 * - the one service a frame can offer that its owner would otherwise have to remember on every
 * line it draws.
 *
 * It cuts BOTH ways, and that is the point: it also caught the colour wheel's rim marker
 * hanging a fifth of a radius past its own window, which the first Vangopix drew over whatever
 * was behind and never noticed. A frame that clips is a frame that reports.
 */

typedef struct _vng_win_ VNG_WIN;

/* Draws the inside. `area` is the interior in screen pixels, already clipped. */
typedef void (*WIN_DRAW) (SDL_FRect area, void *ctx);

/*
 * The inside's events. Called for a press that lands in the interior, and then for every
 * motion and the release that follow it, until the button comes up - so an owner that has
 * taken hold of something keeps receiving the drag even when the pointer wanders off the
 * window. Returning true on the press is what asks for that.
 *
 * RETURNING FALSE HANDS THE PRESS TO THE WINDOW, which drags. Everything that is not a widget
 * is somewhere to take hold of the frame, and a window whose owner wants nothing at all simply
 * passes NULL here and can be grabbed anywhere inside it.
 */
typedef bool (*WIN_EVENT) (SDL_FRect area, const SDL_Event *e, void *ctx);

/*
 * Opens a window. It is created VISIBLE, and it is never destroyed by being closed - hiding
 * and showing keep its size and whatever the owner does not choose to change. `min` is the
 * smallest its interior may be stretched to.
 */
extern VNG_WIN *win_open (const char *title, SDL_FRect area, SDL_FPoint min,
                          WIN_DRAW draw, WIN_EVENT ev, void *ctx);

/*
 * Puts the scissors down for one thing, and picks them back up.
 *
 * An owner's drawing is clipped to its interior, which is what stops a window spilling onto
 * the sheet. ONE KIND OF THING WANTS OUT: a marker whose body is meant to stand outside the
 * thing it marks - the colour wheel's rim arrows, which press the ring from either side and
 * whose tails hang past it. The first Vangopix drew those over whatever was behind, and that
 * IS the look; the difference is that here it is asked for by name and for one call, instead
 * of being what happens because nobody clipped anything.
 *
 * Always in a pair, and always the narrow way round: unclip, draw the one thing, clip again.
 */
extern void win_unclip (VNG_WIN *w);
extern void win_clip   (VNG_WIN *w);

extern void win_show    (VNG_WIN *w, bool on);

/*
 * Puts the WHOLE window - head bar included - centred on a point, and keeps it reachable.
 *
 * What summoning a window calls with the pointer's position, which is the first Vangopix's
 * behaviour (`{mouse.x - w/2, mouse.y - h/2}` in its tool_core.c). A window that appears
 * where the hand already is, is a window that appears where the work is: the alternative is a
 * fixed corner you then have to drag it out of, every time.
 */
extern void win_place (VNG_WIN *w, float cx, float cy);
extern bool win_visible (VNG_WIN *w);

/* The frontmost visible window, or NULL. Showing one raises it, so this is also "the one that
   was just summoned" - which is what a check needs when the thing it wants to click on comes
   up wherever the pointer happened to be. */
extern VNG_WIN *win_top (void);

/* The interior, in screen pixels - what an owner needs to answer questions about itself
   outside its own draw, like where on the sheet it is looking. */
extern SDL_FRect win_area  (VNG_WIN *w);
extern bool      win_hover (VNG_WIN *w);

/* Offered every event. True when a window took it. Motion is consumed ONLY while a window is
   being dragged or stretched: a drag begun on the sheet that crosses a window - a stroke, a
   pan, a grip pulled across - must not be cut in half by it, which is the same rule the
   sidebar follows. */
extern bool win_event (const SDL_Event *e);

extern void win_draw (void);
extern void win_free (void);

#endif
