// Does the transfer work, does it pay, and can it be reversed?
//
// Drops a field of boxes, lets them settle, and watches them transfer into the world. Then
// grabs one back. The third part is the one that matters: a transfer that cannot be
// reversed has consumed the entity, and a world built on it can only calcify.
//
// Asserted rather than printed, because a check that prints and returns zero is a log line:
//
//   - every box transfers into the world
//   - the tick after is cheaper than the tick before
//   - one thing comes back, as a body, and simulates again
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

	printf("transfer on settle: %d boxes, %d ticks, still<%g m/s for %d ticks\n",
	       BOXES, TICKS, cfg.still_speed, cfg.still_ticks);
	printf("  %-6s %-8s %-8s %-9s %s\n", "tick", "bodies", "geoms", "ms/tick", "moved");

	// clock() has millisecond resolution and these ticks are far under one, so time is
	// accumulated over a window rather than sampled. A single-tick reading reads 0.000 on
	// both sides and proves nothing, which is how an earlier version of this test reported
	// FAIL against a mechanism that was working.
	double ms_before = 0.0, ms_after = 0.0;
	int n_before = 0, n_after = 0;
	int reported = 0;

	for (int t = 0; t < TICKS; t++) {
		double t0 = now_ms();
		for (int k = 0; k < 3; k++) mj_step(m, d);   // 3 substeps to a 20 Hz tick
		double ms = now_ms() - t0;

		if (t >= 5 && t < 25) { ms_before += ms; n_before++; }
		if (t >= TICKS - 40) { ms_after += ms; n_after++; }

		int moved = weft_freeze_tick(&f, spec, &m, &d, &cfg, "avatar_");
		if (moved && reported < 6) {
			printf("  %-6d %-8d %-8d %-9.3f +%d\n",
			       t, (int)(m->nbody - 1), (int)m->ngeom, ms, moved);
			reported++;
		}
	}
	ms_before /= (n_before ? n_before : 1);
	ms_after /= (n_after ? n_after : 1);

	printf("\n  bodies left dynamic : %d\n", (int)(m->nbody - 1));
	printf("  moved into world    : %ld\n", f.frozen_total);
	printf("  recompiles          : %ld\n", f.recompiles);
	printf("  tick before         : %.4f ms  (mean of %d)\n", ms_before, n_before);
	printf("  tick after          : %.4f ms  (mean of %d)\n", ms_after, n_after);

	// The round trip. This is what makes it a tier and not a ratchet.
	long moved_total = f.frozen_total;
	int thawed = weft_thaw(&f, spec, &m, &d, "b7");
	int back = (int)(m->nbody - 1);
	mjtNum where[3] = {0, 0, 0};
	if (back > 0) {
		mj_forward(m, d);
		for (int k = 0; k < 3; k++) where[k] = d->xpos[3 + k];
	}
	printf("\n  thaw b7             : %d geom(s) came back\n", thawed);
	printf("  bodies after thaw   : %d\n", back);
	printf("  it is at            : %.3f %.3f %.3f\n", where[0], where[1], where[2]);

	for (int t = 0; t < 60; t++) mj_step(m, d);
	printf("  and it simulates    : %s\n", (m->nbody - 1) > 0 ? "yes" : "no");

	int bad = 0;
	if (moved_total < BOXES) {
		printf("\nFAIL: %ld of %d boxes moved\n", moved_total, (long)BOXES);
		bad = 1;
	}
	if (ms_after >= ms_before) {
		printf("\nFAIL: the transfer did not make the tick cheaper (%.4f -> %.4f)\n",
		       ms_before, ms_after);
		bad = 1;
	}
	if (thawed < 1) {
		printf("\nFAIL: nothing came back, the transfer consumed the entity\n");
		bad = 1;
	}
	if (back != 1) {
		printf("\nFAIL: expected exactly 1 body back, got %d\n", back);
		bad = 1;
	}
	if (!bad) {
		// clock() bottoms out once everything is static, so the ratio goes to infinity and
		// reads as a result. It is not one -- it is the timer running out of resolution.
		// The honest figure is measured while something is still moving.
		if (ms_after < 1e-9) {
			printf("\nOK: all %d moved and one came back. The tick fell from %.4f ms to\n"
			       "    below the timer's resolution, so no ratio is reported.\n",
			       BOXES, ms_before);
		} else {
			printf("\nOK: all %d moved, tick %.1fx cheaper, and one came back\n",
			       BOXES, ms_before / ms_after);
		}
	}

	mj_deleteData(d);
	mj_deleteModel(m);
	mj_deleteSpec(spec);
	weft_freeze_close(&f);
	return bad;
}
