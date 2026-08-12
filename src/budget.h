// What fits in a zone.
//
// Split out of `ward.h` because reading the zone's entity budget should not require a database
// header. `ward.h` includes `sqlite3.h`, since a ward is a SQLite database; the budget is not a
// storage fact and everything that has to respect it is not storage — the fanout, the interest
// filter, the physics, and the benchmark that asks how many of them fit on one core. A header
// that costs a dependency is a header people copy the number out of instead, which is exactly
// what `CLAUDE.md` forbids.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef QUEEN_BUDGET_H
#define QUEEN_BUDGET_H

// A ward is a zone. `AbyssalSLA.lean` sizes one at `entitiesPerZone`, and
// `AuthorityInterest.lean` reserves `InterestCapacity` of it for ghosts a neighbour replicates
// in — those are not this ward's to spend, so a Spark comes out of the rest.
#define WARD_ENTITIES 1800
#define WARD_HEADROOM 400
#define WARD_AUTHORITY (WARD_ENTITIES - WARD_HEADROOM)

#endif
