// The ward, as its interactor sees it.
//
// This exists so `interactor.c` can be a translation unit of its own. It is the state an
// interactor acts on, and nothing here knows what a socket is — which is the whole point of
// the split: `thirdparty/interactor` says a command in and reply bytes out, and this says what
// the Queen's commands are about.
//
// The game itself stays in `queen.c`. Only what an interactor has to reach is declared here.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef QUEEN_WARD_H
#define QUEEN_WARD_H

#include "rng.h"

#include <sqlite3.h>
#include <stdint.h>

#define MAX_SPARKS 64

typedef struct {
	int64_t x, y, z;
} place_t;

typedef struct {
	sqlite3 *db;
	int id;
	int purse;
	int wear;
	place_t at; // where the Frame is standing, in micrometres
} spark_t;

typedef struct {
	sqlite3 *ward;
	spark_t sparks[MAX_SPARKS];
	int nsparks;
	rng_t rng;
	int cycle;
	int treasury;
	int debt;
	int issued;  // every scrip that ever existed
	int found;   // every piece of salvage this ward accounts for
	int retired; // every scrip paid to the Debt Clock
	int spent;   // every scrip paid out of the ward for a venue
	int ward_no; // which ward of the game this is, and the owner in a wire id
	char prefix[64]; // what this ward's databases are called, so a Spark can still arrive later
	uint64_t seed;
	uint64_t seq; // counts what this ward has named, and feeds the UUIDs
} gyre_t;

int scalar(sqlite3 *db, const char *sql);
int found_ward(gyre_t *g, const char *prefix, int nsparks, uint64_t seed, int base);
int build_venue(gyre_t *g, int i);
int pay(gyre_t *g, spark_t *s, int amount, const char *item, const char *kind);
int cycle(gyre_t *g);
int honest(gyre_t *g, const char *when);
void close_ward(gyre_t *g);

// How many more Sparks this ward could hold before its entities fall out of a subscriber's
// slice. `SPARKS_PER_WARD` is derived from the venue table, which is `queen.c`'s, so this is
// an accessor rather than a constant a header could carry.
int ward_room(const gyre_t *g);

#endif
