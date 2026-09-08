#ifndef VANGOPIX_COLOUR_H
#define VANGOPIX_COLOUR_H

#include "vangopix.h"

/*
 * THE COLOUR WINDOW: WHERE A COLOUR THAT IS NOT ON THE SHEET YET COMES FROM.
 *
 * Everything else about colour here reads it off the drawing - CTRL absorbs, M averages what
 * is in hand, SHIFT+R rolls. All of those need a colour to already exist. This is the one
 * place a new one is made, which is exactly the gap it fills and the whole of its job.
 *
 * IT IS NOT A PALETTE PANEL PARKED DOWN THE SIDE. The author's own account of the work is
 * that a pixel artist settles on eight to sixteen colours and then picks from the drawing
 * constantly - so the tiles are not where the work happens, and a window summoned to
 * ESTABLISH those colours and then put away is the shape that matches. C raises it.
 *
 * IT IS THE FIRST VANGOPIX'S WHEEL, and the argument for a square instead was wrong.
 *
 * A square lays hue out along a straight edge, which cuts the circle at an arbitrary point -
 * red - and hides the wrap. HUE IS CIRCULAR, and on a ring the geometry carries the colour
 * theory with it: complementary colours sit opposite, analogous ones sit next to each other,
 * and a triad is a triangle. A bar throws all of that away and gives back only precision,
 * which the four sliders already provide.
 *
 * The design is kept whole because it is good:
 *
 *   - a RING and not a disc, hue at full saturation and value;
 *   - THE HOLE HOLDS THE RESULT - the colour being chosen sits in the middle of the thing
 *     choosing it, so the eye never travels to find out what the wheel just did;
 *   - TWO POINTERS: one at the hue in hand, saying where you ARE, and one following the
 *     pointer while it is over the wheel, saying where you would GO. One arrow could only do
 *     one of those;
 *   - FOUR SLIDERS beside it, each showing its channel across the whole range with the others
 *     held, so what is under the pointer is what would be got.
 *
 * The alpha slider is a real control and not an afterthought, because in this program nothing
 * IS a colour: the second slot starts transparent, rubbing out is drawing with nothing, and a
 * half transparent shade is a thing people want on purpose.
 *
 * WHICH OF THE TWO COLOURS A PRESS FILLS IS SAID BY THE MOUSE BUTTON: left fills colour 1,
 * right fills colour 2. That is the first Vangopix's own arrangement - `if (mouse_left)
 * color_front = color; if (mouse_right) color_back = color;` - and it is the rule this whole
 * program already runs on.
 *
 * SO THERE IS NOTHING IN HERE FOR CHOOSING BETWEEN THEM, and nothing showing them either. Two
 * chips inside this window would be a third way of saying what the mouse already says, beside
 * a readout at the bottom of the screen that shows both all the time.
 *
 * The window FOLLOWS the slot rather than owning it, so a colour absorbed with CTRL out on
 * the sheet appears here without anything having to be told.
 */

extern void colour_toggle  (void);
extern bool colour_visible (void);
extern void colour_free    (void);

#endif
