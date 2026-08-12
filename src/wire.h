// What the ward calls things, and where it puts them.
//
// Moved out of `queen.c` so a test can reach it. These are the parts of the game that are
// arithmetic on an id and nothing else: no database, no RNG, no clock. That is what makes them
// worth testing on their own, and it is also why they can live in a header the way `rng.h`
// does — a translation unit that includes this links nothing.
//
// Nothing here draws from the RNG. A place derived from an id leaves every existing number in
// the game exactly where it was, which is why places could be added to a seeded game at all.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef QUEEN_WIRE_H
#define QUEEN_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// A Spark, a venue and a contract are entities, and an entity has a place. The unit is the
// micrometre, because that is what `XRGridEntityPacket` carries and what the interest filter
// in `fabric-fanout-edge` compares. Keeping the same unit here means the ward joins the zone
// model with no conversion and no second opinion about scale.
#define UM(m) ((int64_t)(m) * 1000000)

// The Commons is the upper deck and the Under-Market is thirty metres below it. RFD 0085 puts
// three venues in each.
#define COMMONS_UM UM(0)
#define UNDER_UM UM(-30)

typedef struct {
	int64_t x, y, z;
} place_t;

// ── Names for things ──────────────────────────────────────────────────────────
//
// An id here has two jobs and they do not fit in one field.
//
// Durable identity is a UUID, because a Spark carries its salvage when it migrates and the
// receiving ward must not be able to hand out the same name. Reserving a numeric range per
// ward would work until a ward exhausted its range, and it would fail by collision rather than
// error. A UUID has no range to exhaust.
//
// It cannot be a UUIDv7, because v7 takes its first 48 bits from the wall clock and this game
// must replay identically from a seed. RFC 9562 keeps version 8 for exactly this: a custom
// layout. So the shape is v7's, with the cycle standing in for the timestamp, and the rest
// filled from a hash of the facts that already determine the game. Two runs of one seed
// therefore produce the same names, and the ids still sort in the order things happened.
//
// Wire identity is separate and is composite. `XRGridEntityPacket` carries a `uint32_t
// global_id` and a `class_owner` that is already `(class << 24) | owner`, so the packet's own
// idiom is a composite key. A hash would be wrong there: 32 bits collides at about 65 thousand
// entities on the birthday bound, and a wire id is a primary key. Packing the ward and a local
// counter is exact instead of probable.
//
// One ward's entities all share the one local space, because `global_id` is the primary key
// for everything a subscriber is sent and not one key per class. So the classes take fixed
// ranges, and a venue and a Spark can never arrive under the same name.
#define WIRE_LOCAL_BITS 20
#define WIRE_LOCAL_MAX (1u << WIRE_LOCAL_BITS)
#define WIRE_WARD_MAX (1u << (32 - WIRE_LOCAL_BITS))

#define WIRE_QUEEN 0u
#define WIRE_VENUE_BASE 1u         // + the venue's index; there are six
#define WIRE_SPARK_BASE 0x100u     // + the Spark's id, which is unique across the whole game
#define WIRE_CONTRACT_BASE 0x1000u // + the board row, which starts again each cycle

enum { CLASS_SPARK = 1, CLASS_VENUE = 2, CLASS_CONTRACT = 3, CLASS_QUEEN = 4 };

static inline uint64_t mix64(uint64_t x) {
	x += 0x9e3779b97f4a7c15ULL;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	return x ^ (x >> 31);
}

// A UUIDv8 whose high 48 bits are the cycle, so the name says when the thing appeared.
// `cls` rather than `class`: this header is C, but a test that includes it is C++, and there
// the word is a keyword. The enum below keeps its names.
static inline void uuid8(char out[37], uint64_t seed, int ward, int cls, int cycle,
                         uint64_t seq) {
	const uint64_t h = mix64(seed ^ mix64(((uint64_t)ward << 40) ^ ((uint64_t)cls << 32) ^ seq));
	const uint64_t g = mix64(h ^ 0xd1b54a32d192ed03ULL);
	const uint64_t ts = (uint64_t)(uint32_t)cycle & 0xffffffffffffULL;

	snprintf(out, 37, "%08x-%04x-8%03x-%04x-%012llx", (unsigned)(ts >> 16),
	         (unsigned)(ts & 0xffff), (unsigned)((h >> 52) & 0xfff),
	         (unsigned)(0x8000 | ((h >> 36) & 0x3fff)), (unsigned long long)(g & 0xffffffffffffULL));
}

// The wire name. Exact, and it fails loudly rather than wrapping — masking the local part
// would be the same failure the numeric range had: two entities under one name, discovered
// only by whatever the collision broke.
static inline uint32_t wire_id(int ward, uint32_t local) {
	if (ward < 0 || (uint32_t)ward >= WIRE_WARD_MAX || local >= WIRE_LOCAL_MAX) {
		fprintf(stderr,
		        "a wire id for ward %d, local %u, does not fit: a ward is %u and a local is %u\n",
		        ward, local, WIRE_WARD_MAX, WIRE_LOCAL_MAX);
		exit(2);
	}
	return ((uint32_t)ward << WIRE_LOCAL_BITS) | local;
}

static inline uint32_t class_owner(int cls, int ward) {
	return ((uint32_t)cls << 24) | ((uint32_t)ward & 0xffffff);
}

// ── Where things stand ────────────────────────────────────────────────────────

// Where a Spark stands when it has no contract. Spread across the Commons by id, with two
// coprime strides so consecutive ids do not land together.
static inline place_t spark_home(int id) {
	place_t p;
	p.x = UM((id * 37) % 51 - 25);
	p.y = COMMONS_UM;
	p.z = UM((id * 23) % 51 - 25);
	return p;
}

// Where the work is. A contract is a marker at the place it names, and half of them are down
// in the Under-Market.
static inline place_t contract_place(int id) {
	place_t p;
	p.x = UM((id * 61) % 101 - 50);
	p.y = (id % 2) ? UNDER_UM : COMMONS_UM;
	p.z = UM((id * 47) % 101 - 50);
	return p;
}

// A name with an apostrophe in it, fit for SQL. The Gyre's own venues are called things
// like Cycle's End Tavern, and renaming them to dodge a quote would be letting the string
// escaping choose the fiction.
static inline const char *quoted(const char *in, char *out, size_t cap) {
	size_t j = 0;
	for (size_t i = 0; in[i] && j + 2 < cap; i++) {
		if (in[i] == '\'') out[j++] = '\'';
		out[j++] = in[i];
	}
	out[j] = '\0';
	return out;
}

#endif
