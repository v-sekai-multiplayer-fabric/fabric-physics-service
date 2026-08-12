// A Queen of the Gyre: a ward, its Sparks, and a clock that wants paying.
//
//   queen play <cycles> [seed]     play a ward and print what became of it
//   queen check <cycles> [seed]    play it twice and hold it to its invariants
//   queen serve <port> [seed]      hold a ward open and take commands from players
//
// The game is the database. There is no renderer and no engine: a cycle is a
// transaction, the ward is a SQLite database over the store plane's VFS, and every Spark is
// a database of their own. What you can see of the game is what you can SELECT.
//
// That is not a stunt. This is a settlement game in the shape of `My Life as a King`, where
// the monarch commissions and then waits: the Queen never takes a contract, and everyone
// else acts on their own. A loop like that is a state machine over days with nothing in the
// critical path to draw, which is the one game shape genuinely better as a database than as
// an engine.
//
// The setting is RFD 0085's, so none of it is borrowed. Sparks are digitised people in
// rented Frames on a failing ring-station; the Under-Market and the Commons are where they
// gather; the Debt Clock is what an automated creditor is owed. The antagonist is not a dark
// lord. It is entropy and compound interest, which is harder to stab.
//
// Why it lives here. `fabric-store-domain` had no workload and the store plane had no
// tenant, so the claim that commits scale with the number of actors had never once run. A
// ward of N Sparks is N databases committing every cycle, and paying one of them is a
// transfer between two databases — which is the parallel commit protocol, exercised as
// ordinary play rather than by a fixture built to exercise it.
//
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include "interactor.h"
#include "planner.h"
#include "rng.h"
#include "transport_tcp.h"
#include "ward.h"
#include "wire.h"
#include "wt.h"

#include <sqlite3.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

int weft_fdb_start(const char *cluster_file);
void weft_fdb_stop(void);
int weft_vfs_register(int make_default);
int weft_txn_begin(unsigned long long *txnid);
int weft_txn_join(sqlite3 *db, unsigned long long txnid);
int weft_txn_commit(unsigned long long txnid);
int weft_txn_abort(unsigned long long txnid);

// How many databases one group commit may hold. This mirrors `TXN_MAX_PARTS` in the store
// plane's `fdb_vfs.c`, which enforces it: a `weft_txn_join` past the limit answers
// SQLITE_FULL rather than overrunning.
#define TXN_MAX_PARTS 16

#define BOARD_SIZE 6

// ── What fits in a ward, and what fits in one slice ───────────────────────────
//
// Two budgets, and the ward used to have one. `SPARKS_PER_WARD` was `SLICE_ENTITIES -
// FIXED_ENTITIES`, so a datagram's width decided how large a world is. The zone's own budget
// decides it now, less the venues, the Queen and a full board of nine.
#define FIXED_ENTITIES (NVENUES + 1 + BOARD_SIZE + 3)
#define SPARKS_PER_WARD (WARD_AUTHORITY - FIXED_ENTITIES)

// What one subscriber is sent in one tick. `MAX_SLICE_ENTITIES` in
// `fabric-fanout-edge/src/fanout.cpp` is 64 and `fanout_one` stops there without saying so:
// the receiver recovers the count as `len / 100`, so a truncated slice looks like a small one.
//
// A zone does not fit in a slice and is not meant to — `InterestCapacity` alone is 400. Which
// entities reach a subscriber is the fanout's choice and no longer this file's, and the ward
// cannot make it because it does not know where a subscriber is standing. Until `fanout_one`
// selects, the venues are no longer guaranteed a place in the slice. That guarantee was the
// derivation above, and the derivation was the bug.
#define SLICE_ENTITIES 64

// ── The clock ─────────────────────────────────────────────────────────────────
//
// The ward keeps the fabric's clock, in the fabric's units. `PBVH_SIM_TICK_HZ` is 20 in
// `predictive_bvh.h`, the crowd plane publishes at 20, and the zone tick was written against the
// same number, so a ward that invented a rate of its own would be the one thing in the stack
// that had to be converted before it could be compared.
//
// A tick is an integer, and so is everything measured in them. `zf_zonetick_run` takes a
// `tick_count` rather than a float `dt` for the same reason the wire does: a velocity is a
// per-tick displacement, and a fractional tick has nothing to multiply.
//
// This clock is real time and it is the only one here that is. It says how often the ward
// publishes what it holds, and nothing else.
//
// A cycle is not made of ticks. A day in the Gyre passes when the Queen's cycle runs, which is
// game time, and the two must not be related: deriving one from the other would tie the rate the
// debt compounds at to the rate the network publishes at, so a ward on a busy machine would owe
// less by the end. It would also cost the replay check, because `check` plays one seed twice and
// compares a fingerprint — and a cycle that advanced on wall-clock time replays differently on a
// slower machine, which turns the invariant into an anecdote about how fast the run went.
#define WARD_TICK_HZ 20

// How often somebody hears the Broadcast Row and comes. Slow enough that the Row is a long
// investment rather than a switch, and on the cycle count rather than the RNG, so an arrival
// is in the same place on a replay.
#define ARRIVAL_CYCLES 12

// How many Sparks the chronicle will name before it stops naming any of them.
#define SPARKS_SHOWN 8

// ── The ward ──────────────────────────────────────────────────────────────────
//
// Six venues, three in the Commons and three in the Under-Market, taken from RFD 0085's
// map. Each changes how the Sparks behave rather than granting a number: a tavern makes
// them bolder because they rest, a chapel makes failure survivable, the Rails put more work
// on the board. The Queen's whole game is choosing what to build and in what order.

typedef struct {
	const char *name;
	int cost;
	const char *effect;
} venue_t;

static const venue_t VENUES[] = {
    {"Cycle's End Tavern", 120, "rest, so a Spark will take a longer odds contract"},
    {"Splicer's Den", 200, "frames repaired, so wear stops ending careers"},
    {"Transit Rails", 260, "more contracts reach the board"},
    {"Exchange Plaza", 340, "salvage sells for what it is worth"},
    {"Chapel of the Backup", 420, "a failure costs a cycle, not a Spark"},
    {"Broadcast Row", 500, "word gets out, and Sparks arrive"},
};
#define NVENUES ((int)(sizeof VENUES / sizeof VENUES[0]))

// The map. Three venues on the Commons deck and three in the Under-Market, laid out so no two
// sit inside one interest box by accident: `ENTITY_EXT_UM` widens every entity by half a
// metre, and these are twenty metres apart.
static const place_t VENUE_PLACES[NVENUES] = {
    {UM(-20), COMMONS_UM, UM(10)}, // Cycle's End Tavern
    {UM(0), COMMONS_UM, UM(18)},   // Splicer's Den
    {UM(20), COMMONS_UM, UM(10)},  // Transit Rails
    {UM(-20), UNDER_UM, UM(-10)},  // Exchange Plaza
    {UM(0), UNDER_UM, UM(-18)},    // Chapel of the Backup
    {UM(20), UNDER_UM, UM(-10)},   // Broadcast Row
};

// Contracts, from RFD 0085's catalogue. Combat is one of the nine and it is avoidable,
// because that setting says exploration carries a session and the fighting is the minority.
// A ward that only ever fought would be a different game and a worse fit for the fiction.
static const char *KINDS[] = {"scavenge", "hack",     "repair", "delivery", "survey",
                              "courier",  "listening", "salvage", "decommission"};
#define NKINDS ((int)(sizeof KINDS / sizeof KINDS[0]))

static int run(sqlite3 *db, const char *sql) {
	char *err = NULL;
	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
		fprintf(stderr, "%s -> %s\n", sql, err ? err : "?");
		sqlite3_free(err);
		return 1;
	}
	return 0;
}

int scalar(sqlite3 *db, const char *sql) {
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
	int v = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int(st, 0) : -1;
	sqlite3_finalize(st);
	return v;
}

// The same, for a place. A micrometre coordinate does not fit in an int once the ward is more
// than about two kilometres across, and reading one back through `scalar` would truncate it
// quietly.
static int64_t scalar64(sqlite3 *db, const char *sql) {
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
	int64_t v = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int64(st, 0) : 0;
	sqlite3_finalize(st);
	return v;
}

static sqlite3 *open_db(const char *name) {
	sqlite3 *db = NULL;
	if (sqlite3_open_v2(name, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "weft_fdb")) {
		fprintf(stderr, "open %s failed\n", name);
		return NULL;
	}
	// What `fdb_vfs.c` asks of a caller. The journal must stay in memory because the commit
	// is already atomic, and exclusive locking stops SQLite re-reading page one on every
	// read transaction, which over a network database is a round trip for every query.
	if (run(db, "PRAGMA journal_mode=MEMORY")) return NULL;
	if (run(db, "PRAGMA locking_mode=EXCLUSIVE")) return NULL;
	return db;
}

// ── Founding ──────────────────────────────────────────────────────────────────

// One Spark, with a database of their own. This is founding for the eight the ward opens
// with, and it is also arriving for the ones the Broadcast Row brings later — the same act
// either way, which is the point of a Spark being a database rather than a row in one.
//
// A ward may not take more than its zone can hold. Beyond that the answer is a second ward,
// not a fuller one.
static int join_ward(gyre_t *g, int id) {
	char name[128], sql[512];
	if (g->nsparks >= MAX_SPARKS || g->nsparks >= SPARKS_PER_WARD) return 1;

	snprintf(name, sizeof name, "%s-spark-%d.db", g->prefix, id);
	spark_t *s = &g->sparks[g->nsparks];
	s->id = id;
	s->purse = 0;
	s->wear = 0;
	s->at = spark_home(id);
	if (!(s->db = open_db(name))) return 1;
	if (run(s->db, "DROP TABLE IF EXISTS spark; DROP TABLE IF EXISTS held;")) return 1;
	if (run(s->db, "CREATE TABLE spark(id INT PRIMARY KEY, purse INT, wear INT, cycles INT,"
	               "                   x INT, y INT, z INT, wire INT, owner INT);"
	               "CREATE TABLE held(item TEXT PRIMARY KEY, kind TEXT, cycle INT)"))
		return 1;
	// A Spark's local part is its id, which is already unique across the whole game, so a
	// Spark that migrates keeps the same local and only changes wards.
	snprintf(sql, sizeof sql,
	         "INSERT INTO spark VALUES (%d, 0, 0, 0, %" PRId64 ", %" PRId64 ", %" PRId64
	         ", %u, %u)",
	         id, s->at.x, s->at.y, s->at.z, wire_id(g->ward_no, WIRE_SPARK_BASE + (uint32_t)id),
	         class_owner(CLASS_SPARK, g->ward_no));
	if (run(s->db, sql)) return 1;

	g->nsparks++;
	return 0;
}

// `base` is the id of this ward's first Spark. A Spark keeps its id when it migrates, so the
// ids have to be unique across every ward of the game rather than only inside one.
int found_ward(gyre_t *g, const char *prefix, int nsparks, uint64_t seed, int base) {
	char name[128], sql[512];

	snprintf(name, sizeof name, "%s-ward.db", prefix);
	if (!(g->ward = open_db(name))) return 1;
	snprintf(g->prefix, sizeof g->prefix, "%s", prefix);

	// The ward's own number, settled before anything is named. It is the owner half of every
	// wire id here and one of the facts a UUID is derived from, so a Spark that migrates keeps
	// names no other ward can hand out.
	g->ward_no = base / SPARKS_PER_WARD;

	if (run(g->ward, "DROP TABLE IF EXISTS ward; DROP TABLE IF EXISTS venue;"
	                 "DROP TABLE IF EXISTS board; DROP TABLE IF EXISTS ledger;"))
		return 1;
	// The place columns are the game's own answer to "where". A client that draws the ward
	// reads them, and the interest filter compares them, so they live in the database rather
	// than in the process that happens to be running.
	//
	// `wire` and `owner` are the same answer to "which". They are what a subscriber gets in an
	// `XRGridEntityPacket`, so a client can match a packet to the row it came from with a
	// `SELECT` rather than by guessing from a position — which is the premise of the whole
	// domain, and the reason a wire id is a column and not a thing the process computes and
	// keeps to itself.
	if (run(g->ward,
	        "CREATE TABLE ward(cycle INT, treasury INT, debt INT, issued INT, retired INT);"
	        "CREATE TABLE venue(id INT PRIMARY KEY, name TEXT, cost INT, built INT,"
	        "                   x INT, y INT, z INT, wire INT UNIQUE, owner INT);"
	        "CREATE TABLE board(id INTEGER PRIMARY KEY, cycle INT, kind TEXT, payout INT,"
	        "                   risk INT, taken INT, outcome TEXT, x INT, y INT, z INT,"
	        "                   wire INT UNIQUE, owner INT);"
	        "CREATE TABLE ledger(id INTEGER PRIMARY KEY, cycle INT, what TEXT, amount INT,"
	        "                    spark INT)"))
		return 1;

	for (int i = 0; i < NVENUES; i++) {
		char safe[96];
		const place_t p = VENUE_PLACES[i];
		snprintf(sql, sizeof sql,
		         "INSERT INTO venue VALUES (%d, '%s', %d, 0, %" PRId64 ", %" PRId64
		         ", %" PRId64 ", %u, %u)",
		         i, quoted(VENUES[i].name, safe, sizeof safe), VENUES[i].cost, p.x, p.y, p.z,
		         wire_id(g->ward_no, WIRE_VENUE_BASE + (uint32_t)i),
		         class_owner(CLASS_VENUE, g->ward_no));
		if (run(g->ward, sql)) return 1;
	}

	// The ward opens owing more than it holds, which is the situation RFD 0085 describes and
	// the reason anybody works here.
	g->treasury = 300;
	g->debt = 4000;
	g->issued = g->treasury;
	g->retired = 0;
	g->cycle = 0;
	g->found = 0;
	g->seq = 0;
	g->seed = seed;
	rng_seed(&g->rng, seed);

	snprintf(sql, sizeof sql, "INSERT INTO ward VALUES (0, %d, %d, %d, 0)", g->treasury,
	         g->debt, g->issued);
	if (run(g->ward, sql)) return 1;

	for (int i = 0; i < nsparks; i++) {
		if (join_ward(g, base + i)) return 1;
	}
	return 0;
}

// `SPARKS_PER_WARD` is derived from the venue table, which is this file's, so a caller outside
// it asks rather than carries a copy of the number. `MAX_SPARKS` bounds the array and this
// bounds the zone; whichever is nearer is the answer, because `join_ward` refuses at both.
int ward_room(const gyre_t *g) {
	const int room = (MAX_SPARKS < SPARKS_PER_WARD ? MAX_SPARKS : SPARKS_PER_WARD) - g->nsparks;
	return room > 0 ? room : 0;
}

static int built(gyre_t *g, int venue) {
	char sql[128];
	snprintf(sql, sizeof sql, "SELECT built FROM venue WHERE id = %d", venue);
	return scalar(g->ward, sql) != 0;
}

// Money turned into a place. The Queen reaches this through her planner and a player reaches
// it by typing `/commission`, and it is one function because the accounting must not depend on
// who asked: a venue bought by hand that skipped the ledger would break `honest()` a cycle
// later, somewhere else, where nothing would point back here.
int build_venue(gyre_t *g, int i) {
	char sql[256];
	if (i < 0 || i >= NVENUES) return 1;
	if (g->treasury < VENUES[i].cost) return 1;
	if (built(g, i)) return 1;

	g->treasury -= VENUES[i].cost;
	// It leaves the ward. Somebody outside built the thing, and the scrip went with them: a
	// venue is not a place to keep money, it is money turned into a place.
	g->spent += VENUES[i].cost;
	snprintf(sql, sizeof sql, "UPDATE venue SET built = %d WHERE id = %d", g->cycle, i);
	if (run(g->ward, sql)) return 1;
	snprintf(sql, sizeof sql,
	         "INSERT INTO ledger(cycle, what, amount, spark) VALUES (%d, 'commission', %d, -1)",
	         g->cycle, -VENUES[i].cost);
	return run(g->ward, sql);
}

// ── The Queen's one decision ──────────────────────────────────────────────────
//
// She commissions, and then she waits. That is the whole of her agency and it is deliberate:
// in the game this is shaped after, a monarch never swings a sword, and the interesting
// question is which building to pay for while the debt compounds.
//
// The policy is a plan rather than a prompt, because a game that plays in CI has nobody to
// ask — and a plan rather than a sort, because sorting was what was wrong with it. This used
// to walk `VENUES[]` and buy the first affordable thing, and that array is in price order, so
// her whole game was a sort by cost standing where the game should have been.
//
// The ward now goes to an HTN planner as a state, and what comes back is the venue at the
// head of a plan. `src/planner.cpp` holds the tasks and the methods. Every write below is
// unchanged: the planner never touches a database, and the accounting was never the part
// that was wrong.
static int commission(gyre_t *g) {
	char sql[256];
	ward_view_t view;
	memset(&view, 0, sizeof view);

	view.cycle = g->cycle;
	view.treasury = g->treasury;
	view.debt = g->debt;
	view.nsparks = g->nsparks;
	// She keeps enough back to pay everyone for a cycle. A ward that cannot pay its Sparks
	// loses them, and a ward with no Sparks earns nothing and owes the same. It is a
	// precondition of commissioning now rather than a `continue` in a loop, which is something
	// a planner can reason around instead of something that silently skips.
	view.reserve = 25 * g->nsparks;
	view.room_to_grow = ward_room(g);
	for (int i = 0; i < NVENUES; i++) {
		snprintf(sql, sizeof sql, "SELECT built FROM venue WHERE id = %d", i);
		view.built[i] = scalar(g->ward, sql);
		view.cost[i] = VENUES[i].cost;
	}
	for (int i = 0; i < g->nsparks; i++) {
		if (g->sparks[i].wear > view.worst_wear) view.worst_wear = g->sparks[i].wear;
	}

	int i = -1;
	if (queen_plan(&view, &i)) {
		// The planner could not answer at all. That is not a Queen who chose to wait, and
		// letting it pass as one is how a game stops replaying with nobody the wiser.
		fprintf(stderr, "cycle %d: the Queen could not plan\n", g->cycle);
		return -2;
	}
	if (i < 0 || i >= NVENUES) return -1;
	return build_venue(g, i) ? -1 : i;
}

// ── The board ─────────────────────────────────────────────────────────────────

static int post_board(gyre_t *g) {
	char sql[512];
	// The Rails put more work within reach. A venue that changes how many contracts exist
	// is worth more than one that changes a number on each.
	const int n = BOARD_SIZE + (built(g, 2) ? 3 : 0);

	if (run(g->ward, "DELETE FROM board")) return 1;
	for (int i = 0; i < n; i++) {
		const char *kind = KINDS[rng_below(&g->rng, NKINDS)];
		const int risk = 10 + (int)rng_below(&g->rng, 55);
		// Riskier work pays more, and the Plaza pays what salvage is actually worth.
		int payout = 18 + risk / 2 + (int)rng_below(&g->rng, 20);
		if (built(g, 3)) payout += payout / 4;
		// The marker goes where the work is. `i` rather than the row id, so the same seed
		// puts the same contract in the same place on a replay.
		const place_t p = contract_place(g->cycle * BOARD_SIZE + i);
		snprintf(sql, sizeof sql,
		         "INSERT INTO board(cycle, kind, payout, risk, taken, outcome, x, y, z)"
		         " VALUES (%d, '%s', %d, %d, -1, NULL, %" PRId64 ", %" PRId64 ", %" PRId64 ")",
		         g->cycle, kind, payout, risk, p.x, p.y, p.z);
		if (run(g->ward, sql)) return 1;

		// The wire name follows the row, because the row id is what the local part is drawn
		// from and SQLite only settles it on the insert. The board is emptied every cycle and
		// the ids start again, so a contract's wire name is reused; that is right for a marker
		// that only exists for one cycle, and durable identity is the UUID's job.
		const sqlite3_int64 row = sqlite3_last_insert_rowid(g->ward);
		snprintf(sql, sizeof sql,
		         "UPDATE board SET wire = %u, owner = %u WHERE id = %lld",
		         wire_id(g->ward_no, WIRE_CONTRACT_BASE + (uint32_t)row),
		         class_owner(CLASS_CONTRACT, g->ward_no), (long long)row);
		if (run(g->ward, sql)) return 1;
	}
	return 0;
}

// A Spark reads the board and chooses. Nobody assigns them: they weigh the payout against
// the risk and against how worn their Frame is, and a tired Spark takes the safe job.
//
// This is where the venues earn their cost. The Tavern makes a Spark bolder because they
// rested; the Chapel makes a bad outcome survivable, so they will look at the long odds.
static int choose(gyre_t *g, spark_t *s) {
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(g->ward,
	                       "SELECT id, payout, risk FROM board WHERE taken = -1 ORDER BY id",
	                       -1, &st, NULL)
	    != SQLITE_OK)
		return -1;

	int best = -1, best_score = INT_MIN;
	const int nerve = 100 - s->wear + (built(g, 0) ? 25 : 0) + (built(g, 4) ? 20 : 0);

	while (sqlite3_step(st) == SQLITE_ROW) {
		const int id = sqlite3_column_int(st, 0);
		const int payout = sqlite3_column_int(st, 1);
		const int risk = sqlite3_column_int(st, 2);
		// What a Spark thinks a contract is worth: the pay, less the risk weighted by how
		// little nerve they have left. A worn Spark discounts danger heavily.
		const int score = payout * nerve / 100 - risk;
		if (score > best_score) {
			best_score = score;
			best = id;
		}
	}
	sqlite3_finalize(st);

	if (best >= 0) {
		char sql[128];
		snprintf(sql, sizeof sql, "UPDATE board SET taken = %d WHERE id = %d", s->id, best);
		if (run(g->ward, sql)) return -1;
	}
	return best;
}

// The group a cycle commits as. The ward is always a participant; a Spark joins the first
// time the cycle writes to them, which is at most once per contract on the board.
//
// `TXN_MAX_PARTS` is 16, so this holds only because the board caps how many Sparks can act:
// BOARD_SIZE is 6 and the Rails add 3, so at most 9 Sparks are written in a cycle, and with
// the ward that is 10 of the 16. A board that grew past 15 would need the group flushed and
// reopened mid-cycle, and `join` returning SQLITE_FULL is what would say so rather than a
// silent overrun.
typedef struct {
	unsigned long long txn;
	sqlite3 *dbs[TXN_MAX_PARTS]; // the participants that have an open SQLite transaction
	int ndbs;
} group_t;

static int group_join(group_t *grp, sqlite3 *db) {
	for (int i = 0; i < grp->ndbs; i++) {
		if (grp->dbs[i] == db) return 0;
	}
	if (grp->ndbs >= TXN_MAX_PARTS) return 1;
	// Join before the first write. From here this file's syncs stage instead of committing,
	// so a caller cannot join after the fact.
	if (weft_txn_join(db, grp->txn) != SQLITE_OK) return 1;
	if (run(db, "BEGIN")) return 1;
	grp->dbs[grp->ndbs++] = db;
	return 0;
}

static int group_begin(group_t *grp, sqlite3 *ward) {
	memset(grp, 0, sizeof *grp);
	if (weft_txn_begin(&grp->txn) != SQLITE_OK) return 1;
	return group_join(grp, ward);
}

static void group_abort(group_t *grp) {
	for (int i = 0; i < grp->ndbs; i++) {
		sqlite3_exec(grp->dbs[i], "ROLLBACK", NULL, NULL, NULL);
	}
	weft_txn_abort(grp->txn);
}

// Every participant commits its SQLite transaction, which stages its pages, and then the
// group record is written. That order is the protocol: no head has moved until the record
// says the group committed.
static int group_commit(group_t *grp) {
	for (int i = 0; i < grp->ndbs; i++) {
		if (run(grp->dbs[i], "COMMIT")) {
			group_abort(grp);
			return 1;
		}
	}
	return weft_txn_commit(grp->txn) != SQLITE_OK;
}

// ── Paying a Spark ────────────────────────────────────────────────────────────
//
// The ward's treasury and the Spark's purse are two databases, so this is one event that
// has to land in both or in neither. That is the parallel commit protocol, and the game
// reaches it by paying somebody rather than by a test reaching for it deliberately.
//
// This used to open a group of its own, which made a payment its own round trip. It now
// joins the cycle's group, because the payment and the cycle that caused it have no reason
// to be separately durable: a cycle is one transaction, which is what the README always
// claimed and the code did not do.
static int pay(gyre_t *g, group_t *grp, spark_t *s, int amount, const char *item,
               const char *kind) {
	char sql[256];

	if (group_join(grp, s->db)) return 1;

	snprintf(sql, sizeof sql,
	         "INSERT INTO ledger(cycle, what, amount, spark) VALUES (%d, 'wages', %d, %d)",
	         g->cycle, -amount, s->id);
	if (run(g->ward, sql)) return 1;

	snprintf(sql, sizeof sql, "UPDATE spark SET purse = purse + %d WHERE id = %d", amount,
	         s->id);
	if (run(s->db, sql)) return 1;

	if (item) {
		snprintf(sql, sizeof sql, "INSERT INTO held VALUES ('%s', '%s', %d)", item, kind,
		         g->cycle);
		if (run(s->db, sql)) return 1;
	}

	g->treasury -= amount;
	s->purse += amount;
	return 0;
}

// A payment nobody's cycle asked for. A player who types `/pay` is outside the cycle group,
// so this opens one for the payment alone and pays the round trip that a payment inside a
// cycle no longer does. The two sides still land together or not at all, which is the part
// that matters: `group_t` is how a cycle saves round trips, not how a transfer stays whole.
int pay_alone(gyre_t *g, spark_t *s, int amount, const char *item, const char *kind) {
	group_t grp;
	if (group_begin(&grp, g->ward)) return 1;
	if (pay(g, &grp, s, amount, item, kind)) {
		group_abort(&grp);
		return 1;
	}
	if (group_commit(&grp)) {
		// `pay` moved the scrip in memory before the group landed, and the ward outlives a
		// refused command. Put it back, or the next `honest` would report a hole that the
		// databases do not have.
		g->treasury += amount;
		s->purse -= amount;
		return 1;
	}
	return 0;
}

// ── One cycle ─────────────────────────────────────────────────────────────────

int cycle(gyre_t *g) {
	char sql[256];
	group_t grp;
	g->cycle++;

	// A cycle is one transaction. Every write below lands in this group, and the round trip
	// is paid once for the cycle rather than once for each statement.
	if (group_begin(&grp, g->ward)) return 1;

	// -1 is a Queen who held, which is most cycles. -2 is a Queen who could not plan, and that
	// stops the ward rather than passing for the same thing.
	if (commission(g) == -2) goto give_up;
	if (post_board(g)) goto give_up;

	for (int i = 0; i < g->nsparks; i++) {
		spark_t *s = &g->sparks[i];
		const int id = choose(g, s);
		if (id < 0) continue;

		snprintf(sql, sizeof sql, "SELECT payout FROM board WHERE id = %d", id);
		const int payout = scalar(g->ward, sql);
		snprintf(sql, sizeof sql, "SELECT risk FROM board WHERE id = %d", id);
		const int risk = scalar(g->ward, sql);
		snprintf(sql, sizeof sql, "SELECT kind FROM board WHERE id = %d", id);

		const int roll = (int)rng_below(&g->rng, 100);
		const int won = roll >= risk;

		// Income is the only thing that makes scrip. Everything else moves it or destroys
		// it, which is what lets a single sum say whether the ward is honest.
		int earned = 0;
		char item[37];
		const char *found_item = NULL;
		if (won) {
			earned = payout;
			g->treasury += earned;
			g->issued += earned;
			// The salvage gets a name no other ward can hand out, derived from this ward's seed,
			// its number, and how much it has named so far.
			uuid8(item, g->seed, g->ward_no, CLASS_CONTRACT, g->cycle, g->seq++);
			found_item = item;
			g->found++;
			snprintf(sql, sizeof sql,
			         "INSERT INTO ledger(cycle, what, amount, spark) VALUES (%d,'salvage',%d,%d)",
			         g->cycle, earned, s->id);
			if (run(g->ward, sql)) goto give_up;
		}

		// A Frame wears. A cycle's rest takes a little of it back, and the Den takes much
		// more — which is what makes the Den worth two hundred scrip rather than a nicety.
		s->wear += won ? 2 : 5;
		s->wear -= 1;
		if (built(g, 1) && s->wear > 0) s->wear -= 4;
		if (s->wear < 0) s->wear = 0;
		if (s->wear > 90) s->wear = 90;

		snprintf(sql, sizeof sql, "UPDATE board SET outcome = '%s' WHERE id = %d",
		         won ? "kept" : "lost", id);
		if (run(g->ward, sql)) goto give_up;

		// A Spark is paid a share whether or not the contract kept, because a ward that only
		// pays for success does not keep its Sparks for long. The share of nothing is
		// nothing, so a failed cycle still costs them.
		const int wage = won ? earned / 2 : 4;
		if (wage > 0 && g->treasury >= wage) {
			if (pay(g, &grp, s, wage, found_item, "salvage")) goto give_up;
		}

		// The Spark went to the work. This is the move that reaches other players: the Frame
		// leaves the boxes it was in and enters the ones around the contract, and the fan-out
		// leaf decides who hears about it. A local choice with a reach nobody chose.
		snprintf(sql, sizeof sql, "SELECT x FROM board WHERE id = %d", id);
		s->at.x = scalar64(g->ward, sql);
		snprintf(sql, sizeof sql, "SELECT y FROM board WHERE id = %d", id);
		s->at.y = scalar64(g->ward, sql);
		snprintf(sql, sizeof sql, "SELECT z FROM board WHERE id = %d", id);
		s->at.z = scalar64(g->ward, sql);

		if (group_join(&grp, s->db)) goto give_up;
		snprintf(sql, sizeof sql,
		         "UPDATE spark SET wear = %d, cycles = %d, x = %" PRId64 ", y = %" PRId64
		         ", z = %" PRId64 " WHERE id = %d",
		         s->wear, g->cycle, s->at.x, s->at.y, s->at.z, s->id);
		if (run(s->db, sql)) goto give_up;
	}

	// The Debt Clock. It compounds first and is paid second, which is the order that makes
	// it frightening: a quiet cycle still loses ground.
	//
	// It takes a share rather than everything, and that share is the whole tension of the
	// game: every scrip that goes to the creditor is one that did not become a venue, and a
	// venue is what makes the next cycle earn more. Pay it all and the ward never improves;
	// pay none and the interest outruns the work.
	g->debt += g->debt / 100;
	int paid = g->treasury / 6;
	if (paid > g->debt) paid = g->debt;
	if (paid > 0) {
		g->treasury -= paid;
		g->debt -= paid;
		g->retired += paid;
		snprintf(sql, sizeof sql,
		         "INSERT INTO ledger(cycle, what, amount, spark) VALUES (%d,'debt',%d,-1)",
		         g->cycle, -paid);
		if (run(g->ward, sql)) goto give_up;
	}

	// Word gets out, and Sparks arrive. The Broadcast Row cost five hundred scrip and until now
	// did nothing at all, which made it the one venue a Queen who reasons about value would
	// never buy — and a rule that sorted by price bought it anyway, which is how it went so
	// long without an effect.
	//
	// Somebody hears the Row and comes to the ward with an empty purse, so conservation is
	// untouched: an arrival adds a database, not scrip. Arrivals are on the cycle count and
	// draw nothing from the RNG, so the stream is where it was.
	//
	// It stops at the zone's authority budget. At 48 that ceiling was reached in a few hundred
	// cycles; at 1384 no run CI has time for will reach it.
	// The arrival is deliberately outside the group. `join_ward` founds a database and
	// creates its tables, and a brand new participant joining a group that is already open
	// would stage DDL against a head that did not exist when the group began. It draws
	// nothing from the RNG and touches no scrip, so running it after the commit leaves the
	// replay and the conservation sum exactly where they were.
	const int arriving =
	    built(g, 5) && g->nsparks < SPARKS_PER_WARD && g->cycle % ARRIVAL_CYCLES == 0;

	snprintf(sql, sizeof sql,
	         "UPDATE ward SET cycle=%d, treasury=%d, debt=%d, issued=%d, retired=%d",
	         g->cycle, g->treasury, g->debt, g->issued, g->retired);
	if (run(g->ward, sql)) goto give_up;

	if (group_commit(&grp)) return 1;

	if (arriving) {
		const int id = g->ward_no * SPARKS_PER_WARD + g->nsparks;
		if (join_ward(g, id)) return 1;
		printf("  cycle %d: word got out, and Spark %d arrived\n", g->cycle, id);
	}
	return 0;

give_up:
	group_abort(&grp);
	return 1;
}

// ── What the ward must never do ───────────────────────────────────────────────

static int purses(gyre_t *g) {
	int total = 0;
	for (int i = 0; i < g->nsparks; i++) {
		total += scalar(g->sparks[i].db, "SELECT purse FROM spark");
	}
	return total;
}

// Conservation. Scrip is made only by salvage and destroyed only by the Debt Clock, so what
// exists must be somewhere: in the treasury or in somebody's purse. A sum that does not
// balance is a payment that landed on one side of a two-database transfer.
int honest(gyre_t *g, const char *when) {
	const int held = g->treasury + purses(g) + g->retired + g->spent;
	if (held != g->issued) {
		printf("BROKEN at %s: treasury %d + purses %d + retired %d + spent %d = %d,"
		       " issued %d\n",
		       when, g->treasury, purses(g), g->retired, g->spent, held, g->issued);
		return 1;
	}
	// Salvage is unique. An item in two purses is a page that committed twice.
	int items = 0;
	for (int i = 0; i < g->nsparks; i++) items += scalar(g->sparks[i].db, "SELECT count(*) FROM held");
	if (items > g->found) {
		printf("BROKEN at %s: %d items held, only %d ever found\n", when, items, g->found);
		return 1;
	}
	return 0;
}

static void chronicle(gyre_t *g) {
	printf("\n  cycle %d of the ward\n", g->cycle);
	printf("  treasury %d   debt %d   paid to the clock %d   built with %d   issued %d\n",
	       g->treasury, g->debt, g->retired, g->spent, g->issued);
	// In the order she paid for them, which is the Queen's decision made visible. A rule that
	// sorted by price could only ever produce one order; a plan produces the order its methods
	// argue for, and the difference is legible here and nowhere else.
	printf("  venues:");
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(g->ward, "SELECT name FROM venue WHERE built > 0 ORDER BY built, id",
	                       -1, &st, NULL) == SQLITE_OK) {
		while (sqlite3_step(st) == SQLITE_ROW) printf(" %s;", sqlite3_column_text(st, 0));
		sqlite3_finalize(st);
	}
	// Every Spark, or none of them and a note saying so. A list cut off at the first eight
	// reads as the whole ward and is not one, and a ward that grows past eight is now the
	// ordinary case rather than the exception — so the cut-off list would have been wrong more
	// often than right, and wrong in the direction of looking correct.
	printf("\n  sparks: ");
	if (g->nsparks > SPARKS_SHOWN) {
		printf("%d of them, too many to list; they are in %s-spark-N.db\n", g->nsparks,
		       g->prefix);
		return;
	}
	for (int i = 0; i < g->nsparks; i++) {
		printf("[%d purse %d wear %d] ", g->sparks[i].id, g->sparks[i].purse, g->sparks[i].wear);
	}
	printf("\n");
}

void close_ward(gyre_t *g) {
	for (int i = 0; i < g->nsparks; i++) sqlite3_close(g->sparks[i].db);
	sqlite3_close(g->ward);
}

// Every name in a Spark's hands, folded into the running hash. `ORDER BY item` so the digest
// is over the set rather than over whatever order the rows came back in.
static unsigned long long fold_held(sqlite3 *db, unsigned long long h) {
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(db, "SELECT item FROM held ORDER BY item", -1, &st, NULL) != SQLITE_OK)
		return h;
	while (sqlite3_step(st) == SQLITE_ROW) {
		const unsigned char *name = sqlite3_column_text(st, 0);
		for (const unsigned char *p = name; p && *p; p++) h = (h ^ *p) * 1099511628211ULL;
	}
	sqlite3_finalize(st);
	return h;
}

// A number that stands for the whole ward, so two runs can be compared in one line.
static unsigned long long fingerprint(gyre_t *g) {
	unsigned long long h = 1469598103934665603ULL;
	int v[6] = {g->cycle, g->treasury, g->debt, g->issued, g->retired, g->spent};
	for (int i = 0; i < 6; i++) h = (h ^ (unsigned)v[i]) * 1099511628211ULL;
	for (int i = 0; i < g->nsparks; i++) {
		h = (h ^ (unsigned)g->sparks[i].purse) * 1099511628211ULL;
		h = (h ^ (unsigned)g->sparks[i].wear) * 1099511628211ULL;
		// Where a Spark stands is ward state now, so the replay check covers it. A place that
		// the fingerprint ignores is a place two runs could disagree about in silence.
		h = (h ^ (unsigned long long)g->sparks[i].at.x) * 1099511628211ULL;
		h = (h ^ (unsigned long long)g->sparks[i].at.y) * 1099511628211ULL;
		h = (h ^ (unsigned long long)g->sparks[i].at.z) * 1099511628211ULL;
		// And what is in their hands. A name is state the same as a purse is, so the replay
		// check has to cover it: `uuid8` draws nothing from the RNG, and a derivation that
		// silently stopped being deterministic would otherwise leave every number matching.
		h = fold_held(g->sparks[i].db, h);
	}
	return h;
}

// ── More than one ward ────────────────────────────────────────────────────────
//
// A ward holds SPARKS_PER_WARD Sparks, because that is what a zone's authority budget leaves
// after the venues, the Queen and the board. A game larger than that is more than one ward,
// each its own service on its own machine, and FoundationDB underneath them all. `AbyssalSLA`
// puts seven of them on a server of eight cores, which is what `MAX_WARDS` is for.
//
// The join costs nothing to arrange. A Spark is its own database whose pages are in
// FoundationDB, so the receiving ward opens it with no copy and no restore, which is the
// property `thirdparty/store-plane/prove_handoff.c` exists to prove. What a migration has to
// get right is the accounting, not the bytes.

// Move a Spark, and the record of its scrip, from one ward to another.
//
// The purse travels with the Spark, so the wards' books have to travel with it too. Ward A
// issued that scrip and no longer holds it; ward B holds it and never issued it. Moving
// `issued` by the same amount is what keeps each ward's own sum true, and it is the whole
// difference between a migration and a scrip printer.
static int migrate(gyre_t *from, gyre_t *to, int idx) {
	char sql[256];
	if (idx < 0 || idx >= from->nsparks) return 1;
	if (to->nsparks >= MAX_SPARKS) return 1;

	spark_t s = from->sparks[idx];

	from->issued -= s.purse;
	to->issued += s.purse;

	// The salvage in their hands moves with them, so the count of what each ward accounts for
	// has to move as well. Ward A found these and no longer holds them; ward B holds them and
	// never found them.
	const int carried_items = scalar(s.db, "SELECT count(*) FROM held");
	from->found -= carried_items;
	to->found += carried_items;

	snprintf(sql, sizeof sql,
	         "INSERT INTO ledger(cycle, what, amount, spark) VALUES (%d, 'departed', %d, %d)",
	         from->cycle, -s.purse, s.id);
	if (run(from->ward, sql)) return 1;
	snprintf(sql, sizeof sql,
	         "INSERT INTO ledger(cycle, what, amount, spark) VALUES (%d, 'arrived', %d, %d)",
	         to->cycle, s.purse, s.id);
	if (run(to->ward, sql)) return 1;

	// The wire name is drawn again, because it says which ward is sending this entity and that
	// is what changed. A subscriber sees the old id stop and a new one start, which is honest:
	// the Frame is now somebody else's to broadcast. What does not change is the UUID on every
	// piece of salvage in its hands — that is the name that had to survive the move, and the
	// reason the two identities are separate fields rather than one.
	snprintf(sql, sizeof sql, "UPDATE spark SET wire = %u, owner = %u WHERE id = %d",
	         wire_id(to->ward_no, WIRE_SPARK_BASE + (uint32_t)s.id),
	         class_owner(CLASS_SPARK, to->ward_no), s.id);
	if (run(s.db, sql)) return 1;

	for (int i = idx; i + 1 < from->nsparks; i++) from->sparks[i] = from->sparks[i + 1];
	from->nsparks--;
	to->sparks[to->nsparks++] = s;
	return 0;
}

// Conservation across every ward at once. Each ward already checks its own books inside every
// cycle. This is the sum a player can never compute, because a player sees one interest slice
// and this needs all of them.
static int honest_across(gyre_t *w, int n, const char *when) {
	long long held = 0, issued = 0;
	for (int i = 0; i < n; i++) {
		held += w[i].treasury + purses(&w[i]) + w[i].retired + w[i].spent;
		issued += w[i].issued;
	}
	if (held != issued) {
		printf("BROKEN across the game at %s: held %lld, issued %lld\n", when, held, issued);
		return 1;
	}
	return 0;
}

// Every wire name in the game, gathered so they can be compared. `UNIQUE` on the column
// already holds each ward to its own names; what no ward can see is the other wards.
static int gather_wires(gyre_t *g, uint32_t *out, int cap, int n) {
	static const char *const FROM[] = {"SELECT wire FROM venue", "SELECT wire FROM board"};
	for (int q = 0; q < 2; q++) {
		sqlite3_stmt *st;
		if (sqlite3_prepare_v2(g->ward, FROM[q], -1, &st, NULL) != SQLITE_OK) return -1;
		while (sqlite3_step(st) == SQLITE_ROW && n < cap)
			out[n++] = (uint32_t)sqlite3_column_int64(st, 0);
		sqlite3_finalize(st);
	}
	for (int i = 0; i < g->nsparks && n < cap; i++)
		out[n++] = (uint32_t)scalar(g->sparks[i].db, "SELECT wire FROM spark");
	return n;
}

static int by_wire(const void *a, const void *b) {
	const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return (x > y) - (x < y);
}

// No two wards may hand out the same wire name. This is the claim the composite scheme makes,
// and the one a range reservation could not keep: `(ward << 20) | local` is exact, so a duplicate
// here would mean a local part had escaped its range rather than that a hash got unlucky.
static int named_apart(gyre_t *w, int nwards) {
	enum { CAP = 8192 };
	uint32_t *all = malloc(CAP * sizeof *all);
	int n = 0, rc = 1;
	if (!all) return 1;

	for (int i = 0; i < nwards; i++) {
		n = gather_wires(&w[i], all, CAP, n);
		if (n < 0) goto done;
	}
	qsort(all, (size_t)n, sizeof *all, by_wire);
	for (int i = 1; i < n; i++) {
		if (all[i] == all[i - 1]) {
			printf("BROKEN: two entities in the game answer to wire id %u\n", all[i]);
			goto done;
		}
	}
	printf("  %d entities across %d wards, and no two share a name\n", n, nwards);
	rc = 0;
done:
	free(all);
	return rc;
}

static int play(const char *prefix, int cycles, int nsparks, uint64_t seed, gyre_t *out) {
	gyre_t g;
	memset(&g, 0, sizeof g);
	if (found_ward(&g, prefix, nsparks, seed, 0)) return 1;

	for (int c = 0; c < cycles; c++) {
		if (cycle(&g)) {
			close_ward(&g);
			return 1;
		}
		if (honest(&g, "a cycle")) {
			close_ward(&g);
			return 1;
		}
	}
	if (out) *out = g;
	return 0;
}

#define MAX_WARDS 8

// The last Spark of the last ward has to land below the board's range. A build that overran
// would hand a Spark and a contract one `global_id`, which `wire_id` cannot catch because both
// are legal locals.
_Static_assert(WIRE_SPARK_BASE + (unsigned)(MAX_WARDS * SPARKS_PER_WARD) <= WIRE_CONTRACT_BASE,
               "the Sparks of MAX_WARDS wards overrun WIRE_CONTRACT_BASE");
_Static_assert(FIXED_ENTITIES + SPARKS_PER_WARD <= WARD_AUTHORITY,
               "a full ward exceeds the zone's authority budget");

// Play a game too large for one ward. Found as many wards as it takes, split the Sparks
// between them, and run them together. Halfway through, move a Spark from the first ward to
// the second, because a game that shards and cannot migrate has only moved the problem.
static int shard(int cycles, int nsparks, uint64_t seed) {
	const int nwards = (nsparks + SPARKS_PER_WARD - 1) / SPARKS_PER_WARD;
	int rc = 1;

	if (nwards > MAX_WARDS) {
		fprintf(stderr, "%d Sparks wants %d wards, and this build holds %d\n", nsparks, nwards,
		        MAX_WARDS);
		return 2;
	}

	// On the heap: `gyre_t` carries `MAX_SPARKS` Sparks inline, so eight wards is half a
	// megabyte of stack where it was twenty-four kilobytes.
	gyre_t *w = calloc(MAX_WARDS, sizeof *w);
	if (!w) {
		fprintf(stderr, "no room for %d wards\n", nwards);
		return 1;
	}

	for (int i = 0, placed = 0; i < nwards; i++) {
		char prefix[32];
		int take = nsparks - placed;
		if (take > SPARKS_PER_WARD) take = SPARKS_PER_WARD;
		snprintf(prefix, sizeof prefix, "gyre-shard-%d", i);
		// A seed of its own, so two wards do not play the same cycle twice over.
		if (found_ward(&w[i], prefix, take, seed + (uint64_t)i, placed)) {
			free(w);
			return 1;
		}
		placed += take;
	}
	printf("\n  %d Sparks across %d wards, %d to a ward\n", nsparks, nwards, SPARKS_PER_WARD);

	for (int c = 0; c < cycles; c++) {
		for (int i = 0; i < nwards; i++) {
			if (cycle(&w[i])) goto done;
			if (honest(&w[i], "a cycle")) goto done;
		}
		if (honest_across(w, nwards, "a cycle")) goto done;

		// The migration, at the midpoint. The Spark's database is not copied: ward 1 already
		// reaches the same pages through FoundationDB. What moves is the authority over it,
		// and the scrip it is carrying.
		if (nwards > 1 && c == cycles / 2) {
			const int who = w[0].sparks[0].id;
			const int carried = w[0].sparks[0].purse;
			if (migrate(&w[0], &w[1], 0)) {
				printf("  the Spark could not move\n");
				goto done;
			}
			printf("  cycle %d: Spark %d moved to ward 1, carrying %d\n", c, who, carried);
			if (honest_across(w, nwards, "a migration")) goto done;
			// The moment the names are most likely to collide: one Spark is now held by a
			// ward that never founded it.
			if (named_apart(w, nwards)) goto done;
		}
	}

	for (int i = 0; i < nwards; i++) chronicle(&w[i]);
	rc = honest_across(w, nwards, "the end");
	if (rc == 0) rc = named_apart(w, nwards);
	if (rc == 0) printf("\n  every ward is honest, and so is the game they add up to\n");

done:
	for (int i = 0; i < nwards; i++) close_ward(&w[i]);
	free(w);
	return rc;
}

// ── The ward, held open ───────────────────────────────────────────────────────
//
// `play` and `check` found a ward, run the cycles and exit, which is what CI wants and what a
// client cannot attach to. `serve` is the same ward with the exit taken out: it is founded
// once, held open, and moved by commands that arrive rather than by a loop that counts.
//
// One process, and one only. `open_db` sets `PRAGMA locking_mode=EXCLUSIVE` and the Queen is
// the single writer, so this is one machine with `min_machines_running = 1`. That is not a
// deployment preference — a second writer is the failure `check_fence` exists to refuse.
//
// Everything below is deliberately single threaded. A command is a transaction against the
// ward, the commands are serialised by the poll loop, and the whole game is a state machine
// over days with nothing in the critical path to draw. A thread per client would buy nothing
// and would put two writers where the VFS permits one.

// A service is a poll loop over transports, and nothing more. It does not know what a
// command means — that is `interactor.c` — and it does not know what a descriptor is for —
// that is the transport's. What it owns is the ward, which is why it lives here.
#define MAX_FDS (WEFT_TRANSPORT_MAX_FDS + 128)

static int serve(int port, int nsparks, uint64_t seed) {
	struct pollfd fds[MAX_FDS];
	weft_transport_t tr[2];
	int ntr = 0, rc;
	gyre_t g;
	tcp_t *tcp;
	wt_t *wt = NULL;

	memset(&g, 0, sizeof g);
	if (found_ward(&g, "gyre-live", nsparks, seed, 0)) return 1;

	const weft_interactor_t in = ward_interactor(&g);
	if (!(tcp = tcp_open(port, in))) {
		close_ward(&g);
		return 1;
	}
	printf("the ward is open on port %d, with %d Sparks and seed %llu\n", port, nsparks,
	       (unsigned long long)seed);
	fflush(stdout);

	// The transport a browser can reach, when it has a certificate to present. picoquic takes
	// file paths and not PEM buffers, so a deployment that keeps the PEM in a secret writes it
	// to disk first — `gateway-edge/TRANSPORT.md` records that, and so does `fly/entrypoint.sh`.
	//
	// Absent a cert this is the TCP line server alone, and it says so rather than pretending.
	// That is a missing credential and not a door: the same code path runs either way, and the
	// interactor the two are handed is the same one.
	{
		const char *cert = getenv("QUEEN_TLS_CERT"), *key = getenv("QUEEN_TLS_KEY");
		const char *bind_addr = getenv("QUEEN_WT_BIND");
		const char *wt_port = getenv("QUEEN_WT_PORT");
		if (cert && key) {
			wt = wt_open(wt_port ? atoi(wt_port) : port + 1, bind_addr, cert, key, in);
			if (!wt) {
				fprintf(stderr, "the ward has no WebTransport, and a cert was given\n");
				tcp_close(tcp);
				close_ward(&g);
				return 1;
			}
		} else {
			printf("no QUEEN_TLS_CERT and QUEEN_TLS_KEY, so no WebTransport: TCP only\n");
			fflush(stdout);
		}
	}

	// WebTransport goes first, because picoquic's timer is how a connection that has gone quiet
	// is retransmitted to or timed out, and a ward with nobody typing is the ordinary case.
	if (wt) tr[ntr++] = wt_transport(wt);
	tr[ntr++] = tcp_transport(tcp);

	while (!tcp_stopped(tcp) && !(wt && wt_stopped(wt))) {
		int owner[MAX_FDS], n = 0;

		// Asked afresh every time round, because a listener's set changes as clients arrive and
		// a loop that cached the answer would poll a socket that had been closed.
		for (int t = 0; t < ntr; t++) {
			int mine[MAX_FDS];
			const int m = tr[t].fds(tr[t].ctx, mine, MAX_FDS - n);
			for (int i = 0; i < m; i++) {
				fds[n].fd = mine[i];
				fds[n].events = POLLIN;
				owner[n] = t;
				n++;
			}
		}

		// No timeout. A ward with nobody typing burns nothing, and a background tab holds its
		// seat by keepalive rather than by traffic — the browser throttles a hidden tab and the
		// client stops asking for slices, so silence on a socket is the ordinary case and never
		// a reason to drop the subscriber.
		if (poll(fds, (nfds_t)n, -1) < 0) break;

		// A transport that drops a client changes its own set, so the descriptors are read from
		// the array this loop already polled and handed back one at a time. Which one it is is
		// the transport's business: a service that knew a UDP socket from a timer would be a
		// service that knew what QUIC is.
		for (int i = 0; i < n; i++) {
			if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
				tr[owner[i]].ready(tr[owner[i]].ctx, fds[i].fd);
			}
		}
	}

	for (int t = 0; t < ntr; t++) tr[t].close(tr[t].ctx);
	rc = honest(&g, "the end");
	close_ward(&g);
	return rc;
}

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: queen play|check|shard <cycles> [seed] [sparks]\n"
		                "       queen serve <port> [seed] [sparks]\n");
		return 2;
	}
	const char *mode = argv[1];
	const int cycles = atoi(argv[2]);
	const uint64_t seed = (argc > 3) ? strtoull(argv[3], NULL, 10) : 20260811;
	const int nsparks = (argc > 4) ? atoi(argv[4]) : 8;

	// One ward is one zone. Past that the answer is a second ward on its own machine, joined
	// underneath by FoundationDB: the pages are already shared, so a Spark moves between wards
	// with no copy and no restore.
	if (strcmp(mode, "shard") != 0 && nsparks > SPARKS_PER_WARD) {
		fprintf(stderr,
		        "%d Sparks is more than one ward may hold. A zone is %d entities, %d of"
		        " those are the ghost budget, and %d are the venues, the Queen and a full"
		        " board, so a ward holds %d Sparks. Run `queen shard` instead.\n",
		        nsparks, WARD_ENTITIES, WARD_HEADROOM, FIXED_ENTITIES, SPARKS_PER_WARD);
		return 2;
	}

	if (weft_fdb_start(getenv("WEFT_FDB_CLUSTER_FILE"))) {
		fprintf(stderr, "the Gyre has no FoundationDB to run on\n");
		return 1;
	}
	weft_vfs_register(1);

	// `serve` takes a port where the others take a count of cycles, because it runs until it is
	// stopped rather than for a number of days.
	if (strcmp(mode, "serve") == 0) return serve(cycles, nsparks, seed);

	if (strcmp(mode, "shard") == 0) return shard(cycles, nsparks, seed);

	gyre_t a;
	memset(&a, 0, sizeof a);
	if (play("gyre-a", cycles, nsparks, seed, &a)) return 1;
	chronicle(&a);
	const unsigned long long fa = fingerprint(&a);
	close_ward(&a);

	if (strcmp(mode, "check") == 0) {
		// The same seed must make the same ward. Without that a cycle cannot be replayed,
		// and an invariant nobody can replay is an anecdote.
		gyre_t b;
		memset(&b, 0, sizeof b);
		if (play("gyre-b", cycles, nsparks, seed, &b)) return 1;
		const unsigned long long fb = fingerprint(&b);
		close_ward(&b);

		printf("\n  fingerprint %llx and %llx\n", fa, fb);
		if (fa != fb) {
			printf("  BROKEN: one seed made two wards\n");
			return 1;
		}
		printf("  the ward is honest and it replays\n");
	}
	return 0;
}
