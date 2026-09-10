#ifndef VANGOPIX_EXPR_H
#define VANGOPIX_EXPR_H

#include "vangopix.h"

/*
 * A LINE OF ARITHMETIC, ANSWERED.
 *
 * `32*4`, `160-8`, `(64+16)/2`. Blender's number boxes do this and it is the right idea for
 * exactly the reason it is being added here: a sprite offset is almost never a number somebody
 * knows, it is a number somebody WORKS OUT. The sheet says the pose is 32 wide and the fourth
 * one starts at 32*3, and typing that is both faster and self-explaining - come back a week
 * later and the box still says where 96 came from, right up until it is committed.
 *
 * ITS OWN FILE RATHER THAN A FEW LINES IN field.c, and that is the whole point of putting it
 * here: field.c is one line of text inside a window, and "what does this line mean" is a
 * different question from "who has the keyboard". The next thing that wants a number worked
 * out - a resize box, a canvas offset, a scale - takes this and nothing else.
 *
 * WHAT IT IS:
 *
 *   + - * /   with the precedence everybody expects
 *   ( )       nested
 *   - +       as signs, so -8 and 3*-2 both read
 *   1.5       decimals, and the answer is always a double - the caller decides what to do
 *             with the fraction, because an INT box rounds and a speed box does not
 *
 * WHAT IT IS NOT: variables, functions, powers, units. Every one of those is a thing to add
 * the day something asks for it, and each would want a decision about names that nothing here
 * is in a position to make.
 *
 * ---------------------------------------------------------------------------------------
 *
 * IT REFUSES RATHER THAN GUESSES, and that is the half worth stating.
 *
 * `SDL_atoi` reads "32*4" as 32 and says nothing, which is the failure this exists to stop:
 * a box that quietly drops the half of the sum it did not understand is worse than one that
 * cannot do sums at all. So the WHOLE line has to be an expression. "32*" is half a sum and is
 * refused; "32 4" is two numbers and is refused; "" is refused.
 *
 * That answer is what makes it safe to run on EVERY KEYSTROKE. A box being typed into spends
 * most of its life holding half a sum, and a caller that only acts on `true` simply keeps the
 * value it had until the line makes sense again - no flicker, no partial reads.
 *
 * Division by zero and anything that overflows to infinity are refusals too, not answers: an
 * infinity travelling on into a sprite rectangle is a blit nobody can explain, and it would
 * arrive far from the line that made it.
 */
extern bool expr_eval (const char *text, double *out);

#endif
