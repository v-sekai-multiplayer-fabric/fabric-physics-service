// How many players fit on one core.
//
//   bench_players [--players N | --ramp] [--cubes 900] [--per-player 3]
//                 [--sim-hz 60] [--publish-hz 20] [--ticks 100] [--core 0]
//                 [--interest 10] [--stack 0] [--gate] [--log docs/logbook/one_core.md]
//
// The question is `fabric-service-meta#4`'s: 1800 entities, twenty snapshots a second, one core
// — how many people is that? Every number it rests on is asserted somewhere in this repository
// and measured nowhere, so this measures it.
//
// It times the whole tick and not `mj_step`, because the tick is what has to fit in 50 ms:
// simulate, then encode an `XRGridEntityPacket` for every entity, then fan out one slice to each
// subscriber. A benchmark that timed the simulation alone would report a budget the encode and
// the fan-out then spend, and it would report it as headroom.
//
// `--players N` times one size. `--ramp` raises the count until a tick misses the budget and
// reports the last one that held, and whether the core or the ward ran out first.
//
// SPDX-License-Identifier: Apache-2.0

// Windows and Linux both, because both are places this runs. The zone is deployed on Linux and
// the headset is on this desk: `fabric-godot-service` drives an HMD from the Windows binary
// `fabric-godot-core` builds, and a physics service that could only be measured in a container
// could not be measured against the client it is for.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define _GNU_SOURCE
#include <sched.h>
#include <time.h>
#endif

#include "scene.h"

#include "mj_physics.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ── The constants this is measured against ────────────────────────────────────
//
// Read from the file that owns each, never copied: `CLAUDE.md` says read the value from the
// source, and a benchmark with its own idea of the budget would be measuring against a number
// the zone does not use.
#include "budget.h" // WARD_ENTITIES, WARD_HEADROOM, WARD_AUTHORITY

// The slice cap and the packet size are not defined here at all: they are `MAX_SLICE_ENTITIES`
// and `XR_PACKET_SIZE`, owned by the edge and the codec, and this file only reads them.

// ── The wire ──────────────────────────────────────────────────────────────────

// The packet is `lean-entity-packet`'s, emitted from the Lean spec that proves its layout, and
// the fan-out is `fabric-fanout-edge`'s. Neither is written again here. A benchmark with its
// own packet struct and its own idea of interest would report a number for a tick nothing runs:
// nearest-64 is not ghost-AABB overlap, and the two do not cost the same.
#include "fanout_bridge.h"

static int64_t um(double metres) { return (int64_t)llround(metres * 1000000.0); }

// The i16 velocity scale is the packet's own, so it comes from the packet's own header rather
// than from a number retyped here.
static int16_t vel_i16(double metres_per_second, double hz) {
	double per_tick_um = metres_per_second * 1000000.0 / hz;
	double scaled = per_tick_um * 32767.0 / XR_PACKET_V_MAX_PHYSICAL_DEFAULT_UM_PER_TICK;

	if (scaled > 32767.0) scaled = 32767.0;
	if (scaled < -32768.0) scaled = -32768.0;
	return (int16_t)lrint(scaled);
}

// ── The clock ─────────────────────────────────────────────────────────────────

// Monotonic on both, and never the wall clock. A tick measured against a clock that can be
// stepped by NTP is a tick that can come out negative.
static double now_ms(void) {
#ifdef _WIN32
	LARGE_INTEGER f, t;

	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
#else
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

// One core, because the question says one core. Unpinned, the scheduler moves the run between
// cores and the answer includes somebody else's cache.
//
// Failing to pin is worth saying and not worth stopping for: an unpinned run is still a number,
// just a noisier one, and a container with a restricted cpuset is a normal place to be.
static void pin_to_core(int core) {
#ifdef _WIN32
	if (SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << core) == 0)
		fprintf(stderr, "note: could not pin to core %d; the numbers will be noisier\n", core);
#else
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(core, &set);
	if (sched_setaffinity(0, sizeof set, &set) != 0)
		fprintf(stderr, "note: could not pin to core %d; the numbers will be noisier\n", core);
#endif
}

static int cmp_double(const void *a, const void *b) {
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y ? 1 : 0;
}

// ── One tick ──────────────────────────────────────────────────────────────────

typedef struct {
	double simulate, encode, slice, total;
	int ncon;
	size_t sent, bytes; // entities the interest filter passed, and bytes a NIC would carry
} spent_t;

typedef struct {
	int players, cubes, per_player, entities, substeps;
	double publish_hz;
	int64_t interest_um; // half-extent of a subscriber's interest box
	xr_grid_entity_packet_t *packets;
	double *mocap_home;
	long tick;
} run_t;

// The avatars are driven, not simulated: a head and two hands follow a headset and two
// controllers, and where they go arrives from outside every tick. Here that input is a slow
// circle, because what the benchmark needs from it is that the avatars keep moving through the
// cube field — a scene where nothing near a subscriber ever changes would let the encode and
// the interest filter run on cold, settled data that a real session never has.
static void drive_avatars(run_t *r, mjData *d, const mjModel *m) {
	for (int i = 0; i < m->nmocap; i++) {
		double phase = (double)r->tick * 0.05 + i;

		d->mocap_pos[3 * i + 0] = r->mocap_home[3 * i + 0] + 0.3 * sin(phase);
		d->mocap_pos[3 * i + 1] = r->mocap_home[3 * i + 1] + 0.3 * cos(phase);
		d->mocap_pos[3 * i + 2] = r->mocap_home[3 * i + 2];
	}
}

// Encode every entity this ward owns, then fan one slice out to each subscriber.
//
// The fan-out is `fanout_one` itself, ghost-AABB overlap and all, with a counter where a socket
// would be. It is O(entities) per subscriber, and that cost is the point: it is the part of the
// tick that grows with players times entities rather than with entities.
static void one_tick(mj_physics_t *phys, run_t *r, spent_t *spent) {
	const mjModel *m = phys->model;
	const mjData *d = phys->data;
	double t0, t1, t2, t3;

	t0 = now_ms();
	drive_avatars(r, phys->data, m);
	for (int i = 0; i < r->substeps; i++) mj_physics_step(phys);
	r->tick++;
	t1 = now_ms();

	// Body 0 is the world and is not an entity. Everything after it is one, in the order the
	// scene declared them: the cubes, then three bodies for each player.
	for (int e = 0; e < r->entities; e++) {
		int b = e + 1;
		xr_grid_entity_packet_t *pk = &r->packets[e];
		int is_avatar = e >= r->cubes;

		pk->gid = (uint32_t)b;
		pk->pos_um_x = um(d->xpos[3 * b + 0]);
		pk->pos_um_y = um(d->xpos[3 * b + 1]);
		pk->pos_um_z = um(d->xpos[3 * b + 2]);
		pk->vel_x = vel_i16(d->cvel[6 * b + 3], r->publish_hz);
		pk->vel_y = vel_i16(d->cvel[6 * b + 4], r->publish_hz);
		pk->vel_z = vel_i16(d->cvel[6 * b + 5], r->publish_hz);
		pk->hlc = 0;
		// `class_owner` is `(class << 24) | owner`. A cube is owned by nobody until somebody
		// grabs it, which is the article's authority sequence and is not modelled here; what
		// matters to an encode is that the field is written.
		pk->class_owner = is_avatar
		                      ? (uint32_t)((2u << 24) | (unsigned)((e - r->cubes) / r->per_player))
		                      : (uint32_t)(1u << 24);
		pk->sub_index = is_avatar ? (uint32_t)((e - r->cubes) % r->per_player) : (uint32_t)e;
		pk->rot_x = (int16_t)lrint(d->xquat[4 * b + 1] * 32767.0);
		pk->rot_y = (int16_t)lrint(d->xquat[4 * b + 2] * 32767.0);
		pk->rot_z = (int16_t)lrint(d->xquat[4 * b + 3] * 32767.0);
	}
	t2 = now_ms();

	spent->sent = 0;
	spent->bytes = 0;
	for (int s = 0; s < r->players; s++) {
		// Where this subscriber is standing: their head, the first of their three. The ward
		// cannot make this choice, which is why `queen.c` stopped trying to.
		const xr_grid_entity_packet_t *me = &r->packets[r->cubes + s * r->per_player];
		int64_t box[6] = {me->pos_um_x - r->interest_um, me->pos_um_x + r->interest_um,
		                  me->pos_um_y - r->interest_um, me->pos_um_y + r->interest_um,
		                  me->pos_um_z - r->interest_um, me->pos_um_z + r->interest_um};

		spent->sent += bench_fanout_one(box, r->packets, (size_t)r->entities, &spent->bytes);
	}
	t3 = now_ms();

	spent->simulate = t1 - t0;
	spent->encode = t2 - t1;
	spent->slice = t3 - t2;
	spent->total = t3 - t0;
	spent->ncon = d->ncon;
}

// ── A run at one size ─────────────────────────────────────────────────────────

typedef struct {
	double median, worst, simulate, encode, slice;
	int ncon, entities;
	size_t sent, bytes; // what the interest filter passed, summed over subscribers
	int ok;
} result_t;

static int run_at(int players, int cubes, int stack, int pile, int iters, int ls_iters,
                  int per_player, double sim_hz, double publish_hz, int ticks, double interest_m,
                  result_t *out) {
	mj_physics_t phys;
	char error[1024] = {0};
	char *scene;
	run_t r;
	double *totals;
	spent_t spent;
	double sum_sim = 0, sum_enc = 0, sum_sli = 0, worst = 0;
	// MuJoCo's timestep is the simulation rate, not the publish rate. A scene stepped at 20 Hz
	// is a different simulation from one stepped at 60 and published at 20, and it is the
	// second that `fabric-crowd-plane` runs.
	double timestep = 1.0 / sim_hz;

	memset(out, 0, sizeof *out);
	memset(&r, 0, sizeof r);
	r.players = players;
	r.cubes = cubes;
	r.per_player = per_player;
	r.entities = cubes + players * per_player;
	r.substeps = (int)lrint(sim_hz / publish_hz);
	r.publish_hz = publish_hz;
	r.interest_um = (int64_t)llround(interest_m * 1000000.0);
	out->entities = r.entities;

	scene = gaffer_scene(players, cubes, stack, pile, iters, ls_iters, timestep);
	if (!scene) {
		fprintf(stderr, "cannot build a scene of %d players and %d cubes\n", players, cubes);
		return 0;
	}
	if (mj_physics_init(&phys, scene, error, sizeof error) != 0) {
		fprintf(stderr, "mj_physics_init failed: %s\n", error);
		free(scene);
		return 0;
	}
	free(scene);

	if (phys.model->nbody - 1 != r.entities) {
		fprintf(stderr, "the scene has %lld bodies, not the %d asked for\n",
		        (long long)(phys.model->nbody - 1), r.entities);
		mj_physics_close(&phys);
		return 0;
	}

	r.packets = calloc((size_t)r.entities, sizeof *r.packets);
	r.mocap_home = calloc((size_t)(phys.model->nmocap ? phys.model->nmocap : 1) * 3,
	                      sizeof *r.mocap_home);
	totals = calloc((size_t)ticks, sizeof *totals);
	if (!r.packets || !r.mocap_home || !totals) {
		fprintf(stderr, "out of memory at %d players\n", players);
		goto done;
	}

	// Where the avatars started. `mj_makeData` seeds `mocap_pos` from the model, and after the
	// first tick writes to it the model's own copy is no longer where they were put.
	memcpy(r.mocap_home, phys.data->mocap_pos, (size_t)phys.model->nmocap * 3 * sizeof(double));

	// A tenth of the run, thrown away. The cubes are dropped just above the floor and the first
	// ticks are them settling, which is a contact count no running scene has.
	for (int i = 0; i < ticks / 10 + 1; i++) one_tick(&phys, &r, &spent);

	for (int i = 0; i < ticks; i++) {
		one_tick(&phys, &r, &spent);
		totals[i] = spent.total;
		sum_sim += spent.simulate;
		sum_enc += spent.encode;
		sum_sli += spent.slice;
		if (spent.total > worst) worst = spent.total;
		out->ncon = spent.ncon;
		out->sent = spent.sent;
		out->bytes = spent.bytes;
	}

	qsort(totals, (size_t)ticks, sizeof *totals, cmp_double);
	out->median = totals[ticks / 2];
	out->worst = worst;
	out->simulate = sum_sim / ticks;
	out->encode = sum_enc / ticks;
	out->slice = sum_sli / ticks;
	out->ok = 1;

done:
	mj_physics_close(&phys);
	free(r.packets);
	free(r.mocap_home);
	free(totals);
	return out->ok;
}

// ── Writing it down ───────────────────────────────────────────────────────────

// A number without its conditions is not a result, which is what `docs/logbook/` exists to say.
// So a logged row carries every condition the run had, not only the outcome: the same tick is
// 15 ms on this desk and something else on a zone host, and a row that recorded only the
// milliseconds would be a number nobody can place.
//
// It appends and never rewrites. A run that turned out to be invalid stays in the file with the
// entry that says why, because a deleted run teaches nothing twice.
static void log_row(const char *path, const char *label, int players, int cubes, int per_player,
                    double sim_hz, double publish_hz, double interest_m, int ticks,
                    const result_t *res) {
	static const char *HEADER =
	    "| when | run | players | cubes | per player | entities | sim Hz | pub Hz | interest m |"
	    " ticks | median ms | worst ms | simulate | encode | fanout | contacts | sent | bytes |\n"
	    "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
	    " ---: | ---: | ---: | ---: | ---: | ---: |\n";
	char when[32];
	time_t now = time(NULL);
	struct tm tmv;
	FILE *f;
	long size;

	// The header goes in only when the file is new. Appending it every run would put a table
	// head in the middle of a table.
	f = fopen(path, "a+");
	if (!f) {
		fprintf(stderr, "cannot write the logbook at %s\n", path);
		return;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);

#ifdef _WIN32
	localtime_s(&tmv, &now);
#else
	localtime_r(&now, &tmv);
#endif
	strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);

	if (size == 0) fputs(HEADER, f);
	fprintf(f,
	        "| %s | %s | %d | %d | %d | %d | %.0f | %.0f | %.1f | %d | %.2f | %.2f | %.2f |"
	        " %.2f | %.2f | %d | %zu | %zu |\n",
	        when, label, players, cubes, per_player, res->entities, sim_hz, publish_hz, interest_m,
	        ticks, res->median, res->worst, res->simulate, res->encode, res->slice, res->ncon,
	        res->sent, res->bytes);
	fclose(f);
}

// ── Saying it ─────────────────────────────────────────────────────────────────

static void report(int players, int cubes, int per_player, const result_t *res, double budget_ms,
                   double publish_hz) {
	int room = (WARD_AUTHORITY - cubes) / per_player;
	double slice_bits = (double)bench_slice_cap() * XR_PACKET_SIZE * 8.0 * publish_hz;

	printf("players %d, cubes %d   entities %d of %d authority (%d in the ward)\n", players, cubes,
	       res->entities, WARD_AUTHORITY, WARD_ENTITIES);
	printf("tick     %6.2f ms median, %6.2f ms worst, of %.1f ms budget   (%.0f%%)\n",
	       res->median, res->worst, budget_ms, 100.0 * res->median / budget_ms);
	printf("  simulate %6.2f   encode %6.2f   fanout %6.2f     contacts %d\n", res->simulate,
	       res->encode, res->slice, res->ncon);
	// A filter that passes nothing and a filter that fills every slice are different failures,
	// and both of them look like a fast tick.
	printf("interest passed %zu entities in %d slices (%.1f each, cap %zu), %zu bytes a tick\n",
	       res->sent, players, players ? (double)res->sent / players : 0.0, bench_slice_cap(),
	       res->bytes);
	printf("the entity budget allows %d players beside %d cubes; a slice holds %.1f avatars\n",
	       room, cubes, (double)bench_slice_cap() / per_player);
	printf("one subscriber's slice is %.2f Mbps, uncompressed\n", slice_bits / 1000000.0);
}

int main(int argc, char **argv) {
	int per_player = AVATAR_ENTITIES, players = 0, cubes = 900, ticks = 100, core = 0, ramp = 0;
	int gate = 0;
	// How many cubes high. A flat field is a zone nobody has played in; the article's players
	// build towers, and a tower is a coupled chain of contacts the solver carries every step.
	// Timing only the field understates the simulate stage for every topology at once, which is
	// worse than understating one — it makes the comparison look fair while all are flattered.
	int stack = 0;
	// Zero leaves MuJoCo's defaults (100 and 50). Above zero is a deadline on the solver.
	int iters = 0, ls_iters = 0;
	// Pyramids. A reproduction case for the limits work, never a scene to measure with.
	int pile = 0;
	const char *logbook = NULL;
	double sim_hz = 60.0, publish_hz = 20.0;
	// How far a subscriber cares, in metres. Room scale plus reach: the article's players are
	// standing in a play space throwing cubes at each other, not surveying a field. A subscriber
	// who could see everything would make the interest filter a no-op, and the fan-out stage
	// would be timing a copy.
	double interest_m = 10.0;
	double budget_ms;
	result_t r;
	int room;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--ramp")) ramp = 1;
		else if (!strcmp(argv[i], "--gate")) gate = 1;
		else if (i + 1 >= argc) { fprintf(stderr, "%s wants a value\n", argv[i]); return 2; }
		else if (!strcmp(argv[i], "--per-player")) per_player = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--players")) players = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--cubes")) cubes = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--stack")) stack = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--pile")) pile = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--solver-iters")) iters = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--ls-iters")) ls_iters = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--ticks")) ticks = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--core")) core = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--sim-hz")) sim_hz = atof(argv[++i]);
		else if (!strcmp(argv[i], "--publish-hz")) publish_hz = atof(argv[++i]);
		else if (!strcmp(argv[i], "--interest")) interest_m = atof(argv[++i]);
		else if (!strcmp(argv[i], "--log")) logbook = argv[++i];
		else { fprintf(stderr, "no such option: %s\n", argv[i]); return 2; }
	}
	if (per_player < 1 || players < 0 || cubes < 0 || stack < 0 || ticks < 10 || sim_hz < publish_hz ||
	    interest_m <= 0) {
		fprintf(stderr, "those arguments do not make a run\n");
		return 2;
	}
	room = (WARD_AUTHORITY - cubes) / per_player;
	if (room < 1) {
		fprintf(stderr, "%d cubes leaves no room for a player in %d authority entities\n", cubes,
		        WARD_AUTHORITY);
		return 2;
	}

	pin_to_core(core);

	budget_ms = 1000.0 / publish_hz;
	printf("one core, %.0f Hz publish, %.0f Hz simulate, %d entities a player, %d ticks\n",
	       publish_hz, sim_hz, per_player, ticks);
	printf("the budget is %.1f ms a tick\n\n", budget_ms);

	if (!ramp) {
		if (players == 0) players = room;
		if (!run_at(players, cubes, stack, pile, iters, ls_iters, per_player, sim_hz, publish_hz, ticks,
		            interest_m, &r))
			return 1;
		report(players, cubes, per_player, &r, budget_ms, publish_hz);
		if (logbook)
			log_row(logbook, "fixed", players, cubes, per_player, sim_hz, publish_hz, interest_m,
			        ticks, &r);
		// Only `--gate` makes the budget an exit code. CI runs this on a shared runner whose
		// core is somebody else's too, and a benchmark that fails the build when a neighbour is
		// busy is a benchmark that gets deleted. CI checks it runs; a person checks the number.
		return (gate && r.median > budget_ms) ? 1 : 0;
	}

	// Up to what the ward can hold and no further: past that the answer is a second ward rather
	// than a bigger core, so a ramp that kept going would be timing a zone that cannot exist.
	{
		int last_ok = 0;
		result_t last;

		memset(&last, 0, sizeof last);
		for (int n = 1; n <= room; n++) {
			if (!run_at(n, cubes, stack, pile, iters, ls_iters, per_player, sim_hz, publish_hz, ticks,
			                interest_m, &r))
				return 1;
			printf("  %3d players  %6.2f ms median  %6.2f worst  (sim %5.2f enc %5.2f sli %5.2f)"
			       "  contacts %d\n",
			       n, r.median, r.worst, r.simulate, r.encode, r.slice, r.ncon);
			// Every step, not only the one that held. The shape of the curve is the result: a
			// ramp that degrades gently and one that falls off a cliff have the same last-good
			// number and are not the same finding.
			if (logbook)
				log_row(logbook, "ramp", n, cubes, per_player, sim_hz, publish_hz, interest_m,
				        ticks, &r);
			if (r.median > budget_ms) break;
			last_ok = n;
			last = r;
		}
		printf("\n");
		if (last_ok == 0) {
			printf("no player count fits: %d cubes and one avatar already miss %.1f ms\n", cubes,
			       budget_ms);
			return 1;
		}
		report(last_ok, cubes, per_player, &last, budget_ms, publish_hz);
		if (last_ok == room)
			printf("the ward ran out before the core did: %d is the entity budget, not the CPU\n",
			       last_ok);
		else
			printf("the core ran out before the ward did: %d of the %d the budget allows\n",
			       last_ok, room);
	}
	return 0;
}
