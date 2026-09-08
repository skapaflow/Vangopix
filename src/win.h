#ifndef VANGOPIX_WIN_H
#define VANGOPIX_WIN_H

#include "vangopix.h"

/*
 * A WINDOW FRAME, AND DELIBERATELY NOT A WIDGET TOOLKIT.
 *
 * Section 7 says to leave the first Vangopix's src/window/ behind because VagrantUI would
 * replace it. That reason has EXPIRED, and it is worth writing down why rather than quietly
 * doing the opposite:
 *
 *   VagrantUI emits VUI_CMD_RECT and VUI_CMD_TEXT and nothing else (verified in its own
 *   vagrantui.h). The windows this program actually wants are mostly VIEWPORTS ONTO PIXELS -
 *   the 1:1 panel, a colour wheel, a timeline of frames - and a texture is the one thing
 *   those two commands cannot say. That is not a defect in VagrantUI: "rect and text and
 *   nothing else" is exactly what lets it drop into any engine, and a texture handle is
 *   backend-specific, so adding one would cost VagrantUI more than it would gain Vangopix.
 *
 * So the two COMPOSE rather than compete. The frame is ours; the interior belongs to whoever
 * owns the window; and the day a window is genuinely made of text and boxes, VagrantUI draws
 * inside one of these.
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
 * THE INTERIOR IS CLIPPED FOR THE OWNER, so a window cannot spill onto the sheet by
 * accident - the one service a frame can offer that its owner would otherwise have to
 * remember.
 */

typedef struct _vng_win_ VNG_WIN;

/* Draws the inside. `area` is the interior in screen pixels, already clipped. */
typedef void (*WIN_DRAW) (SDL_FRect area, void *ctx);

/*
 * The inside's events. Called for a press that lands in the interior, and then for every
 * motion and the release that follow it, until the button comes up - so an owner that has
 * taken hold of something keeps receiving the drag even when the pointer wanders off the
 * window. Returning true on the press is what asks for that; a window whose owner wants
 * nothing simply passes NULL.
 */
typedef bool (*WIN_EVENT) (SDL_FRect area, const SDL_Event *e, void *ctx);

/*
 * Opens a window. It is created VISIBLE, and it is never destroyed by being closed - hiding
 * and showing keep its position and size, which is what lets a panel be parked somewhere and
 * found there again. `min` is the smallest its interior may be stretched to.
 */
extern VNG_WIN *win_open (const char *title, SDL_FRect area, SDL_FPoint min,
                          WIN_DRAW draw, WIN_EVENT ev, void *ctx);

extern void win_show    (VNG_WIN *w, bool on);
extern bool win_visible (VNG_WIN *w);

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
