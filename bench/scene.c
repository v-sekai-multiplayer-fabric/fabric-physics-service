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

char *gaffer_scene(int players, int cubes, double timestep) {
	out_t o;
	size_t cap;
	int entities = cubes + players * AVATAR_ENTITIES;
	int side;

	if (players < 0 || cubes < 0 || entities < 1 || timestep <= 0) return NULL;

	// About 200 bytes a body, and MJCF is the only thing written here, so one generous slab is
	// cheaper than growing one.
	cap = (size_t)entities * 256 + 4096;
	o.p = malloc(cap);
	if (!o.p) return NULL;
	o.n = 0;
	o.cap = cap;
	o.over = 0;

	say(&o, "<mujoco><option timestep=\"%g\"/>", timestep);
	say(&o, "<worldbody>");
	say(&o, "<geom name=\"floor\" type=\"plane\" size=\"200 200 0.1\"/>");

	// A field rather than a tower. The article's stacks are what a player builds during a
	// session, and a scene that starts as a twenty-metre stack measures the solver holding one
	// up — which is a real cost, but not the one a zone pays for most of its life. Cubes start
	// settled and apart, and the run stirs them.
	side = 1;
	while (side * side < (cubes > 0 ? cubes : 1)) side++;
	for (int i = 0; i < cubes; i++) {
		int col = i % side, row = i / side; // a grid index, so the division is meant to truncate
		double x = col * 0.6 - side * 0.3, y = row * 0.6 - side * 0.3;

		say(&o, "<body name=\"c%d\" pos=\"%g %g 0.3\"><freejoint/>", i, x, y);
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
