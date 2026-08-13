// Does freeze-on-settle actually work, and does it pay?
//
// Drops a field of boxes, runs the tick with freezing on, and reports what it cost before
// and after. The claim under test is the one freeze.h is built on: that a settled world is
// nearly free, and that promoting a body into the worldbody is what makes it so.
//
// Two things this asserts rather than prints, because a check that prints and returns zero
// is a log line:
//
//   - every box eventually freezes
//   - the tick after freezing is cheaper than the tick before
//
// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mujoco/mujoco.h>

#include "freeze.h"

#define BOXES 300
#define TICKS 400

static double now_ms(void) {
	return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

static char *scene_xml(void) {
	size_t cap = 64 * 1024;
	char *s = (char *)malloc(cap);
	int n = snprintf(s, cap,
	    "<mujoco><option timestep=\"0.0166\"><flag sleep=\"enable\"/></option>"
	    "<worldbody><geom name=\"floor\" type=\"plane\" size=\"20 20 0.1\"/>");
	for (int i = 0; i < BOXES; i++) {
		double x = (i % 20) * 0.12 - 1.2;
		double y = ((i / 20) % 20) * 0.12 - 0.9;
		double z = 0.05 + (i / 400) * 0.12;
		n += snprintf(s + n, cap - (size_t)n,
		    "<body name=\"b%d\" pos=\"%g %g %g\"><freejoint/>"
		    "<geom type=\"box\" size=\"0.05 0.05 0.05\" mass=\"0.2\"/></body>", i, x, y, z);
	}
	snprintf(s + n, cap - (size_t)n, "</worldbody></mujoco>");
	return s;
}

int main(void) {
	char err[1024] = {0};
	char *xml = scene_xml();

	mjSpec *spec = mj_parseXMLString(xml, NULL, err, sizeof(err));
	free(xml);
	if (!spec) {
		fprintf(stderr, "parse failed: %s\n", err);
		return 1;
	}
	mjModel *m = mj_compile(spec, NULL);
	if (!m) {
		fprintf(stderr, "compile failed: %s\n", mjs_getError(spec));
		return 1;
	}
	mjData *d = mj_makeData(m);

	weft_freeze_t f;
	weft_freeze_cfg cfg = weft_freeze_defaults();
	if (weft_freeze_init(&f, m) != 0) {
		fprintf(stderr, "freeze init failed\n");
		return 1;
	}

	printf("freeze-on-settle: %d boxes, %d ticks, still<%g m/s for %d ticks\n",
	       BOXES, TICKS, cfg.still_speed, cfg.still_ticks);
	printf("  %-6s %-8s %-8s %-9s %s\n", "tick", "bodies", "geoms", "ms/tick", "frozen");

	// clock() has millisecond resolution and these ticks are far under one, so time is
	// accumulated over a window rather than sampled per tick. A single-tick reading here
	// reads 0.000 both sides and proves nothing, which is how the first version of this
	// test "failed" a mechanism that was working.
	double ms_before = 0.0, ms_after = 0.0;
	int n_before = 0, n_after = 0;
	int reported = 0;

	for (int t = 0; t < TICKS; t++) {
		double t0 = now_ms();
		for (int k = 0; k < 3; k++) mj_step(m, d);   // 3 substeps to a 20 Hz tick
		double ms = now_ms() - t0;

		if (t >= 5 && t < 25) { ms_before += ms; n_before++; }
		if (t >= TICKS - 40) { ms_after += ms; n_after++; }

		int froze = weft_freeze_tick(&f, spec, &m, &d, &cfg, "avatar_");
		if (froze && reported < 6) {
			printf("  %-6d %-8d %-8d %-9.3f +%d\n", t, m->nbody - 1, m->ngeom, ms, froze);
			reported++;
		}
	}
	ms_before /= (n_before ? n_before : 1);
	ms_after /= (n_after ? n_after : 1);

	printf("\n  bodies left dynamic : %d\n", m->nbody - 1);
	printf("  frozen into world   : %ld\n", f.frozen_total);
	printf("  recompiles          : %ld\n", f.recompiles);
	printf("  tick before freezing: %.3f ms\n", ms_before);
	printf("  tick after freezing : %.3f ms\n", ms_after);

	int bad = 0;
	if (f.frozen_total < BOXES) {
		printf("\nFAIL: %ld of %d boxes froze\n", f.frozen_total, BOXES);
		bad = 1;
	}
	if (ms_after >= ms_before) {
		printf("\nFAIL: freezing did not make the tick cheaper (%.3f -> %.3f)\n",
		       ms_before, ms_after);
		bad = 1;
	}
	if (!bad) {
		printf("\nOK: every box froze, and the tick got %.1fx cheaper\n",
		       ms_before / (ms_after > 0 ? ms_after : 1e-6));
	}

	mj_deleteData(d);
	mj_deleteModel(m);
	mj_deleteSpec(spec);
	weft_freeze_close(&f);
	return bad;
}
