#include "expr.h"

/*
 * RECURSIVE DESCENT, WHICH IS WHAT PRECEDENCE IS.
 *
 * Three levels, and the shape of them IS the rule that * binds tighter than +: a sum is made
 * of products, a product is made of signed things, and a signed thing is a number or a whole
 * expression in brackets. There is no precedence TABLE anywhere below, because the call chain
 * already is one - which is why this is thirty lines instead of a state machine.
 *
 * The alternative considered and rejected was the obvious one: walk the string left to right
 * applying each operator as it arrives. That is four lines and it reads 2+3*4 as 20. A number
 * box that gets that wrong is worse than a number box that cannot add.
 */

typedef struct {
	const char *p;
	bool        bad;    /* something did not parse; the answer is void from here on */
	int         deep;   /* how many brackets in - see the guard in `primary` */
} SCAN;

/* How many nested brackets are allowed. The fields this serves hold 32 characters, so 32 is
 * already unreachable from a keyboard - it is here because this is a public entry point and a
 * caller with a longer string must not be able to walk the stack off the end with "((((((". */
#define DEEP_MAX 32

static void skip (SCAN *s)
{
	while (*s->p == ' ' || *s->p == '\t') s->p++;
}

static double sum (SCAN *s);

static double primary (SCAN *s)
{
	skip(s);

	if (*s->p == '(') {
		if (s->deep >= DEEP_MAX) { s->bad = true; return 0.0; }

		s->p++;
		s->deep++;
		double v = sum(s);
		s->deep--;

		skip(s);
		if (*s->p == ')') s->p++;
		else              s->bad = true;   /* an unclosed bracket is half a sum */

		return v;
	}

	/* SDL_strtod and not atof: the END POINTER is the whole reason. atof reads what it can and
	 * says nothing about where it stopped, which is precisely the silence this file exists to
	 * break - see expr.h. */
	char  *end = NULL;
	double v   = SDL_strtod(s->p, &end);

	if (!end || end == s->p) { s->bad = true; return 0.0; }

	s->p = end;
	return v;
}

/* A SIGN IS NOT AN OPERATOR, which is why it lives here and not in `sum`. Written as its own
 * level so -8, 3*-2 and --8 all read, and so that 3-2 is still a subtraction: `sum` looks for
 * the '-' first and only what is left over reaches this. */
static double signed_thing (SCAN *s)
{
	skip(s);

	if (*s->p == '-') { s->p++; return -signed_thing(s); }
	if (*s->p == '+') { s->p++; return  signed_thing(s); }

	return primary(s);
}

static double product (SCAN *s)
{
	double v = signed_thing(s);

	for (;;) {
		skip(s);

		char c = *s->p;
		if (c != '*' && c != '/') return v;

		s->p++;
		double r = signed_thing(s);

		if (c == '*') { v *= r; continue; }

		/* Refused, not answered. An infinity travelling on into a sprite rectangle is a blit
		 * nobody can explain, and it would arrive a long way from the line that made it. */
		if (r == 0.0) { s->bad = true; return 0.0; }
		v /= r;
	}
}

static double sum (SCAN *s)
{
	double v = product(s);

	for (;;) {
		skip(s);

		char c = *s->p;
		if (c != '+' && c != '-') return v;

		s->p++;
		double r = product(s);

		v = (c == '+') ? v + r : v - r;
	}
}

bool expr_eval (const char *text, double *out)
{
	if (!text || !out) return false;

	SCAN s = { text, false, 0 };
	double v = sum(&s);

	skip(&s);

	/*
	 * THE WHOLE LINE, OR NOTHING.
	 *
	 * Anything left over means the line was not an expression: "32*" stops with the operator
	 * unspent, "32 4" stops at the 4, "1.2.3" stops at the second dot. Each of those is a box
	 * mid-keystroke, and reading 32 out of them is exactly the quiet wrong answer that atoi
	 * gives - the caller keeps what it had instead.
	 */
	if (s.bad || *s.p) return false;

	/* What a chain of perfectly legal operators can still arrive at. */
	if (SDL_isnan(v) || SDL_isinf(v)) return false;

	*out = v;
	return true;
}
