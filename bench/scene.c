// The article's scene, as MJCF: a floor, a field of cubes, and avatars that are three tracked
// objects each.
//
// The cubes are simulated and the avatars are not, which is the article's own division rather
// than an economy. A head and two hands follow a headset and two controllers, so their poses
// arrive from outside the simulation every tick; MuJoCo calls a body like that `mocap`, and a
// mocap body has no degrees of freedom but still collides — a hand can still knock a cube over.
// Giving the hands joints and letting the solver decide where they go would be modelling
// something the article does not do, and would charge the benchmark for it.
//
// SPDX-License-Identifier: Apache-2.0

#include "scene.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// Append, and say so if the buffer ran out rather than returning a scene short by a closing tag.
// A truncated MJCF is not a smaller scene; it is one MuJoCo refuses to parse, and the refusal
// arrives as a syntax error about a line nobody wrote.
typedef struct {
	char *p;
	size_t n, cap;
	int over;
} out_t;

// The format check is worth having and MSVC does not have it, so it is asked for where it
// exists rather than dropped where it does not: a wrong conversion here is an MJCF MuJoCo
// rejects with a parse error about a line nobody wrote.
#ifdef __GNUC__
static void say(out_t *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif

static void say(out_t *o, const char *fmt, ...) {
	va_list ap;
	int n;

	if (o->over) return;
	va_start(ap, fmt);
	n = vsnprintf(o->p + o->n, o->cap - o->n, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= o->cap - o->n) {
		o->over = 1;
		return;
	}
	o->n += (size_t)n;
}

char *gaffer_scene(int players, int cubes, int stack, int pile, int iters, int ls_iters,
                   double timestep) {
	out_t o;
	size_t cap;
	int entities = cubes + players * AVATAR_ENTITIES;
	int side;
	int towers;

	if (players < 0 || cubes < 0 || stack < 0 || pile < 0 || iters < 0 || ls_iters < 0 || entities < 1 ||
	    timestep <= 0)
		return NULL;

	// About 200 bytes a body, and MJCF is the only thing written here, so one generous slab is
	// cheaper than growing one.
	cap = (size_t)entities * 256 + 4096;
	o.p = malloc(cap);
	if (!o.p) return NULL;
	o.n = 0;
	o.cap = cap;
	o.over = 0;

	// The solver caps go on `<option>` beside the timestep, because they belong to the same
	// contract: both decide what one step means, and a peer that disagreed about either would be
	// simulating a different world rather than the same world less well.
	say(&o, "<mujoco><option timestep=\"%g\"", timestep);
	if (iters > 0) say(&o, " iterations=\"%d\"", iters);
	if (ls_iters > 0) say(&o, " ls_iterations=\"%d\"", ls_iters);
	say(&o, "/>");
	say(&o, "<worldbody>");
	say(&o, "<geom name=\"floor\" type=\"plane\" size=\"200 200 0.1\"/>");

	// A field, or towers.
	//
	// `stack` 0 is the flat field: every cube touches the floor and nothing else, which is what a
	// zone looks like before anybody has played in it. It is the cheap case and, being cheap, the
	// one that flatters both the solver and determinism.
	//
	// Anything above 1 is what the article's players actually build — "huge stacks of cubes, up to
	// 20 or 30 meters high". A tower is a chain of contacts the solver has to keep upright every
	// step, and the constraint it solves is coupled all the way down: a cube at the bottom feels
	// the fifty above it. That is a different order of work from a grid, and measuring only the
	// grid understates the simulate stage of every topology equally, which is worse than
	// understating one — it makes the comparison look fair while all three are flattered.
	//
	// Towers are laid on the same grid, spaced by the cube's own width plus a margin so that
	// neighbouring towers can lean without starting interpenetrated.
	// Three shapes, and two of them exist to fail.
	//
	// `pile` is pyramids. It is the reproduction case for the limits work: a pyramid stands
	// because every cube rests on others, so it is the arrangement with the most contacts there
	// is — 100 cubes make 1345, 400 make 6435 and run at a fiftieth of realtime. Neither an
	// iteration cap nor the sleep flag moves it.
	//
	// `stack` is columns, which fail differently: fifty cubes is fifty to one, and nothing stands
	// at fifty to one. It ejects rather than topples.
	//
	// Both are kept and both are named, because a sandbox will see them. This is not an
	// adversarial case needing somebody to mean it — with enough players every buildable shape
	// gets built, and a bound demonstrated only on the flat field is a bound demonstrated on the
	// case that was never going to be the problem.
	const int per_pyramid = pile > 1 ? pile * (pile + 1) * (2 * pile + 1) / 6 : 1;
	int per_group = pile > 1 ? per_pyramid : (stack > 1 ? stack : 1);
	towers = (cubes + per_group - 1) / per_group;
	if (towers < 1) towers = 1;
	side = 1;
	while (side * side < towers) side++;
	const double pitch = pile > 1   ? (pile + 2) * CUBE_METRES * 1.6
	                   : stack > 1  ? (stack * CUBE_METRES) * 0.75
	                                : 0.6;
	for (int i = 0; i < cubes; i++) {
		int t = i / per_group;
		int col = t % side, row = t / side;
		double ox = col * pitch - side * pitch * 0.5, oy = row * pitch - side * pitch * 0.5;
		double x = ox, y = oy, z = 0.3;

		if (pile > 1) {
			// Layer L is (pile - L) square. Exactly touching: the gap a column needs to avoid
			// interpenetration is the same gap that makes fifty levels free-fall together.
			int k = i % per_group, level = 0, w = pile;

			while (k >= w * w) { k -= w * w; level++; w--; }
			x = ox + (k % w - (w - 1) * 0.5) * CUBE_METRES;
			y = oy + (k / w - (w - 1) * 0.5) * CUBE_METRES;
			z = 0.2 + level * CUBE_METRES;
		} else if (stack > 1) {
			z = 0.3 + (i % per_group) * (CUBE_METRES + 0.002);
		}

		say(&o, "<body name=\"c%d\" pos=\"%g %g %g\"><freejoint/>", i, x, y, z);
		say(&o, "<geom type=\"box\" size=\"0.2 0.2 0.2\" mass=\"1\"/></body>");
	}

	// Three mocap bodies a player, in among the cubes rather than off to one side: an avatar
	// standing outside the field would never be near anything, and the interest filter the
	// benchmark times would have nothing to choose between.
	for (int p = 0; p < players; p++) {
		int col = p % side, row = p / side;
		double x = col * 0.6 - side * 0.3, y = row * 0.6 - side * 0.3;

		say(&o, "<body name=\"h%d\" mocap=\"true\" pos=\"%g %g 1.6\">", p, x, y);
		say(&o, "<geom type=\"sphere\" size=\"0.12\"/></body>");
		say(&o, "<body name=\"lh%d\" mocap=\"true\" pos=\"%g %g 1.1\">", p, x - 0.25, y);
		say(&o, "<geom type=\"sphere\" size=\"0.06\"/></body>");
		say(&o, "<body name=\"rh%d\" mocap=\"true\" pos=\"%g %g 1.1\">", p, x + 0.25, y);
		say(&o, "<geom type=\"sphere\" size=\"0.06\"/></body>");
	}

	say(&o, "</worldbody></mujoco>");

	if (o.over) {
		free(o.p);
		return NULL;
	}
	return o.p;
}
