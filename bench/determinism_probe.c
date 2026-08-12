// Does this simulation replay identically?
//
//   determinism_probe [--cubes 900] [--players 4] [--ticks 1200] [--sim-hz 60]
//                     [--publish-hz 20] [--emit <file>] [--compare <file>]
//
// Deterministic lockstep sends intent and nothing else, and every peer arrives at the same
// world by simulating it. That only works if the same inputs produce the same state — bit for
// bit, not nearly — so this asks the question before anything is built on the answer.
//
// It is a kill switch on purpose. If MuJoCo does not reproduce inside one process on one
// binary, it will not reproduce across two machines, and lockstep-over-physics is finished as
// an option before a harness exists for it. That is a good hour to spend: the alternative is
// discovering it after building the topology.
//
// What it does NOT answer is cross-platform, which is the question a shipped lockstep actually
// needs. MuJoCo makes no such promise, `mjtNum` is a double whose rounding follows the compiler
// and its flags, and this desk is one compiler. A pass here means "not ruled out locally", and
// the Linux and headset runs are still owed. `--emit` and `--compare` are for exactly that: run
// it on two machines and diff the traces.
//
// SPDX-License-Identifier: Apache-2.0

#include "scene.h"

#include "mj_physics.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FNV-1a over the raw state bytes. A hash rather than a comparison because the interesting
// output is one line per tick that two machines can diff, and 1400 entities of state is 30 kB a
// tick that nobody will read. Where they first differ is the number that matters.
static uint64_t fnv1a(const void *bytes, size_t len) {
	const unsigned char *p = (const unsigned char *)bytes;
	uint64_t h = 1469598103934665603ULL;

	for (size_t i = 0; i < len; i++) {
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	return h;
}

// The same scripted input both worlds get. It must be a pure function of the tick: a run driven
// by anything read from a clock, a random source or the machine would differ for a reason that
// says nothing about the physics, and the probe would blame MuJoCo for its own harness.
static void drive(mj_physics_t *phys, const double *home, long tick) {
	const mjModel *m = phys->model;

	for (int i = 0; i < m->nmocap; i++) {
		double phase = (double)tick * 0.05 + i;

		phys->data->mocap_pos[3 * i + 0] = home[3 * i + 0] + 0.3 * sin(phase);
		phys->data->mocap_pos[3 * i + 1] = home[3 * i + 1] + 0.3 * cos(phase);
		phys->data->mocap_pos[3 * i + 2] = home[3 * i + 2];
	}
}

// The largest absolute difference anywhere in the state, so a divergence can be read as "the
// last bit of one number" or "the world came apart". Those want different responses and a hash
// tells them apart not at all.
static double worst_gap(const mjtNum *a, const mjtNum *b, mjtSize n, mjtSize *at) {
	double worst = 0.0;

	*at = -1;
	for (mjtSize i = 0; i < n; i++) {
		double d = fabs((double)a[i] - (double)b[i]);

		if (d > worst) {
			worst = d;
			*at = i;
		}
	}
	return worst;
}

int main(int argc, char **argv) {
	int cubes = 900, players = 4, ticks = 1200;
	double sim_hz = 60.0, publish_hz = 20.0;
	const char *emit_path = NULL, *compare_path = NULL;

	for (int i = 1; i < argc; i++) {
		if (i + 1 >= argc) { fprintf(stderr, "%s wants a value\n", argv[i]); return 2; }
		else if (!strcmp(argv[i], "--cubes")) cubes = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--players")) players = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--ticks")) ticks = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--sim-hz")) sim_hz = atof(argv[++i]);
		else if (!strcmp(argv[i], "--publish-hz")) publish_hz = atof(argv[++i]);
		else if (!strcmp(argv[i], "--emit")) emit_path = argv[++i];
		else if (!strcmp(argv[i], "--compare")) compare_path = argv[++i];
		else { fprintf(stderr, "no such option: %s\n", argv[i]); return 2; }
	}
	if (cubes < 0 || players < 0 || ticks < 1 || sim_hz < publish_hz) {
		fprintf(stderr, "those arguments do not make a run\n");
		return 2;
	}

	const double timestep = 1.0 / sim_hz;
	const int substeps = (int)lrint(sim_hz / publish_hz);

	// Two worlds from one scene string. Same text, so any difference is the simulation's and not
	// the generator's — building the MJCF twice would also test the generator, which is a
	// separate question and would muddy this one.
	char *scene = gaffer_scene(players, cubes, timestep);
	if (!scene) {
		fprintf(stderr, "cannot build a scene of %d players and %d cubes\n", players, cubes);
		return 1;
	}

	mj_physics_t a, b;
	char err[1024] = {0};

	if (mj_physics_init(&a, scene, err, sizeof err) != 0) {
		fprintf(stderr, "world A failed: %s\n", err);
		free(scene);
		return 1;
	}
	if (mj_physics_init(&b, scene, err, sizeof err) != 0) {
		fprintf(stderr, "world B failed: %s\n", err);
		mj_physics_close(&a);
		free(scene);
		return 1;
	}
	free(scene);

	// `mj_stateSize` returns `mjtSize`, which is `int64_t`. Taking it as an `int` truncates on
	// a scene big enough to matter and is the kind of narrowing that only shows up once the
	// world is large — which is exactly the case this probe exists to run.
	const mjtSize nstate = mj_stateSize(a.model, mjSTATE_INTEGRATION);
	mjtNum *sa = (mjtNum *)malloc((size_t)nstate * sizeof(mjtNum));
	mjtNum *sb = (mjtNum *)malloc((size_t)nstate * sizeof(mjtNum));
	double *home_a = (double *)calloc((size_t)(a.model->nmocap ? a.model->nmocap : 1) * 3,
	                                  sizeof(double));
	double *home_b = (double *)calloc((size_t)(b.model->nmocap ? b.model->nmocap : 1) * 3,
	                                  sizeof(double));
	FILE *emit = NULL;

	if (!sa || !sb || !home_a || !home_b) {
		fprintf(stderr, "out of memory\n");
		return 1;
	}
	memcpy(home_a, a.data->mocap_pos, (size_t)a.model->nmocap * 3 * sizeof(double));
	memcpy(home_b, b.data->mocap_pos, (size_t)b.model->nmocap * 3 * sizeof(double));

	if (emit_path && !(emit = fopen(emit_path, "w"))) {
		fprintf(stderr, "cannot write %s\n", emit_path);
		return 1;
	}

	printf("%d cubes, %d players, %lld entities, %d ticks at %.0f/%.0f Hz (%d substeps)\n",
	       cubes, players, (long long)(a.model->nbody - 1), ticks, sim_hz, publish_hz, substeps);
	printf("state is %lld mjtNum under mjSTATE_INTEGRATION (%zu bytes)\n\n",
	       (long long)nstate, (size_t)nstate * sizeof(mjtNum));
	if (emit) fprintf(emit, "tick,hash\n");

	int diverged_at = -1;
	double gap = 0.0;
	mjtSize gap_at = -1;

	for (long t = 0; t < ticks; t++) {
		drive(&a, home_a, t);
		drive(&b, home_b, t);
		for (int s = 0; s < substeps; s++) {
			mj_physics_step(&a);
			mj_physics_step(&b);
		}

		mj_getState(a.model, a.data, sa, mjSTATE_INTEGRATION);
		mj_getState(b.model, b.data, sb, mjSTATE_INTEGRATION);

		const size_t bytes = (size_t)nstate * sizeof(mjtNum);
		if (emit) fprintf(emit, "%ld,%016llx\n", t, (unsigned long long)fnv1a(sa, bytes));

		if (diverged_at < 0 && memcmp(sa, sb, bytes) != 0) {
			diverged_at = (int)t;
			gap = worst_gap(sa, sb, nstate, &gap_at);
			// Keep running rather than stopping. Whether a divergence stays in the last bits or
			// grows into a different world is the difference between a rounding artefact and an
			// unusable topology, and stopping at the first one cannot tell them apart.
			printf("DIVERGED at tick %d: worst |a-b| = %.17g at state[%lld]\n", diverged_at, gap,
			       (long long)gap_at);
		}
	}

	if (diverged_at >= 0) {
		gap = worst_gap(sa, sb, nstate, &gap_at);
		printf("\nat tick %d the worst difference is %.17g at state[%lld]\n", ticks - 1, gap,
		       (long long)gap_at);
	}

	if (emit) fclose(emit);

	// Two traces from two machines, or two runs, held to the same standard as one process.
	if (compare_path) {
		FILE *other = fopen(compare_path, "r");

		if (!other) {
			fprintf(stderr, "cannot read %s\n", compare_path);
			return 1;
		}
		printf("\ncomparing against %s\n", compare_path);
		char line[256];
		long tick = 0;
		int mismatch = -1;

		if (!fgets(line, sizeof line, other)) { fclose(other); return 1; } // the header row
		// The emitted trace is world A's, so this re-derives A's hashes rather than trusting the
		// run to have kept every one of them in memory.
		mj_physics_close(&a);
		mj_physics_close(&b);
		char *again = gaffer_scene(players, cubes, timestep);
		mj_physics_init(&a, again, err, sizeof err);
		free(again);
		memcpy(home_a, a.data->mocap_pos, (size_t)a.model->nmocap * 3 * sizeof(double));

		for (long t = 0; t < ticks && fgets(line, sizeof line, other); t++) {
			drive(&a, home_a, t);
			for (int s = 0; s < substeps; s++) mj_physics_step(&a);
			mj_getState(a.model, a.data, sa, mjSTATE_INTEGRATION);
			unsigned long long theirs = 0;
			long theirtick = 0;

			if (sscanf(line, "%ld,%llx", &theirtick, &theirs) != 2) continue;
			if (mismatch < 0 && fnv1a(sa, (size_t)nstate * sizeof(mjtNum)) != theirs) {
				mismatch = (int)t;
				printf("traces differ first at tick %d\n", mismatch);
			}
			tick = t;
		}
		fclose(other);
		if (mismatch < 0) {
			printf("traces agree for %ld ticks\n", tick + 1);
		}
		mj_physics_close(&a);
		free(sa); free(sb); free(home_a); free(home_b);
		return mismatch < 0 ? 0 : 1;
	}

	mj_physics_close(&a);
	mj_physics_close(&b);
	free(sa); free(sb); free(home_a); free(home_b);

	if (diverged_at < 0) {
		printf("\nidentical for all %d ticks\n", ticks);
		printf("lockstep is not ruled out here. It is still owed a run on Linux and on the\n");
		printf("headset: same binary is the floor, not the question.\n");
		return 0;
	}
	printf("\nnot reproducible in one process. Deterministic lockstep over this physics is out;\n");
	printf("intent-driven state machines are a different and cheaper question.\n");
	return 1;
}
