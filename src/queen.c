// A Queen of the Gyre: a ward, its Sparks, and a clock that wants paying.
//
//   queen play <cycles> [seed]     play a ward and print what became of it
//   queen check <cycles> [seed]    play it twice and hold it to its invariants
//
// The game is the database. There is no renderer, no client and no engine: a cycle is a
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

#include "rng.h"

#include <sqlite3.h>
#include <inttypes.h>
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

#define MAX_SPARKS 64
#define BOARD_SIZE 6

// ── Where things are ──────────────────────────────────────────────────────────
//
// A Spark, a venue and a contract are entities, and an entity has a place. The unit is the
// micrometre, because that is what `XRGridEntityPacket` carries and what the interest filter
// in `fabric-fanout-edge` compares. Keeping the same unit here means the ward joins the zone
// model with no conversion and no second opinion about scale.
//
// Every place below is derived from an id by integer arithmetic. None of it draws from the
// RNG, so adding places left every existing number in the game exactly where it was.
#define UM(m) ((int64_t)(m) * 1000000)

// The Commons is the upper deck and the Under-Market is thirty metres below it. RFD 0085 puts
// three venues in each.
#define COMMONS_UM UM(0)
#define UNDER_UM UM(-30)

// How many entities one subscriber can be sent in one tick. `MAX_SLICE_ENTITIES` in
// `fabric-fanout-edge/src/fanout.cpp` is 64, and `fanout_one` stops at the cap without
// saying so: the receiver recovers the count as `len / 100`, so a truncated slice looks
// exactly like a small one.
//
// The ward's fixed entities come out of that budget first. Six venues, the Queen, and a full
// board of nine once the Transit Rails are up. What is left is what the Sparks may have.
#define SLICE_ENTITIES 64
#define FIXED_ENTITIES (NVENUES + 1 + BOARD_SIZE + 3)
#define SPARKS_PER_WARD (SLICE_ENTITIES - FIXED_ENTITIES)

typedef struct {
	int64_t x, y, z;
} place_t;

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

// Where a Spark stands when it has no contract. Spread across the Commons by id, with two
// coprime strides so consecutive ids do not land together.
static place_t spark_home(int id) {
	place_t p;
	p.x = UM((id * 37) % 51 - 25);
	p.y = COMMONS_UM;
	p.z = UM((id * 23) % 51 - 25);
	return p;
}

// Where the work is. A contract is a marker at the place it names, and half of them are down
// in the Under-Market.
static place_t contract_place(int id) {
	place_t p;
	p.x = UM((id * 61) % 101 - 50);
	p.y = (id % 2) ? UNDER_UM : COMMONS_UM;
	p.z = UM((id * 47) % 101 - 50);
	return p;
}

// Contracts, from RFD 0085's catalogue. Combat is one of the nine and it is avoidable,
// because that setting says exploration carries a session and the fighting is the minority.
// A ward that only ever fought would be a different game and a worse fit for the fiction.
static const char *KINDS[] = {"scavenge", "hack",     "repair", "delivery", "survey",
                              "courier",  "listening", "salvage", "decommission"};
#define NKINDS ((int)(sizeof KINDS / sizeof KINDS[0]))

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
	int retired; // every scrip paid to the Debt Clock
	int spent;   // every scrip paid out of the ward for a venue
	int next_item;
} gyre_t;

// A name with an apostrophe in it, fit for SQL. The Gyre's own venues are called things
// like Cycle's End Tavern, and renaming them to dodge a quote would be letting the string
// escaping choose the fiction.
static const char *quoted(const char *in, char *out, size_t cap) {
	size_t j = 0;
	for (size_t i = 0; in[i] && j + 2 < cap; i++) {
		if (in[i] == '\'') out[j++] = '\'';
		out[j++] = in[i];
	}
	out[j] = '\0';
	return out;
}

static int run(sqlite3 *db, const char *sql) {
	char *err = NULL;
	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
		fprintf(stderr, "%s -> %s\n", sql, err ? err : "?");
		sqlite3_free(err);
		return 1;
	}
	return 0;
}

static int scalar(sqlite3 *db, const char *sql) {
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

static int found_ward(gyre_t *g, const char *prefix, int nsparks, uint64_t seed) {
	char name[128], sql[512];

	snprintf(name, sizeof name, "%s-ward.db", prefix);
	if (!(g->ward = open_db(name))) return 1;

	if (run(g->ward, "DROP TABLE IF EXISTS ward; DROP TABLE IF EXISTS venue;"
	                 "DROP TABLE IF EXISTS board; DROP TABLE IF EXISTS ledger;"))
		return 1;
	// The place columns are the game's own answer to "where". A client that draws the ward
	// reads them, and the interest filter compares them, so they live in the database rather
	// than in the process that happens to be running.
	if (run(g->ward,
	        "CREATE TABLE ward(cycle INT, treasury INT, debt INT, issued INT, retired INT);"
	        "CREATE TABLE venue(id INT PRIMARY KEY, name TEXT, cost INT, built INT,"
	        "                   x INT, y INT, z INT);"
	        "CREATE TABLE board(id INTEGER PRIMARY KEY, cycle INT, kind TEXT, payout INT,"
	        "                   risk INT, taken INT, outcome TEXT, x INT, y INT, z INT);"
	        "CREATE TABLE ledger(id INTEGER PRIMARY KEY, cycle INT, what TEXT, amount INT,"
	        "                    spark INT)"))
		return 1;

	for (int i = 0; i < NVENUES; i++) {
		char safe[96];
		const place_t p = VENUE_PLACES[i];
		snprintf(sql, sizeof sql,
		         "INSERT INTO venue VALUES (%d, '%s', %d, 0, %" PRId64 ", %" PRId64
		         ", %" PRId64 ")",
		         i, quoted(VENUES[i].name, safe, sizeof safe), VENUES[i].cost, p.x, p.y, p.z);
		if (run(g->ward, sql)) return 1;
	}

	// The ward opens owing more than it holds, which is the situation RFD 0085 describes and
	// the reason anybody works here.
	g->treasury = 300;
	g->debt = 4000;
	g->issued = g->treasury;
	g->retired = 0;
	g->cycle = 0;
	g->next_item = 1;
	rng_seed(&g->rng, seed);

	snprintf(sql, sizeof sql, "INSERT INTO ward VALUES (0, %d, %d, %d, 0)", g->treasury,
	         g->debt, g->issued);
	if (run(g->ward, sql)) return 1;

	g->nsparks = nsparks > MAX_SPARKS ? MAX_SPARKS : nsparks;
	for (int i = 0; i < g->nsparks; i++) {
		snprintf(name, sizeof name, "%s-spark-%d.db", prefix, i);
		spark_t *s = &g->sparks[i];
		s->id = i;
		s->purse = 0;
		s->wear = 0;
		s->at = spark_home(i);
		if (!(s->db = open_db(name))) return 1;
		if (run(s->db, "DROP TABLE IF EXISTS spark; DROP TABLE IF EXISTS held;")) return 1;
		if (run(s->db, "CREATE TABLE spark(id INT PRIMARY KEY, purse INT, wear INT, cycles INT,"
		               "                   x INT, y INT, z INT);"
		               "CREATE TABLE held(item INT PRIMARY KEY, kind TEXT, cycle INT)"))
			return 1;
		snprintf(sql, sizeof sql,
		         "INSERT INTO spark VALUES (%d, 0, 0, 0, %" PRId64 ", %" PRId64 ", %" PRId64 ")",
		         i, s->at.x, s->at.y, s->at.z);
		if (run(s->db, sql)) return 1;
	}
	return 0;
}

// ── The Queen's one decision ──────────────────────────────────────────────────
//
// She commissions, and then she waits. That is the whole of her agency and it is deliberate:
// in the game this is shaped after, a monarch never swings a sword, and the interesting
// question is which building to pay for while the debt compounds.
//
// The policy is a rule rather than a prompt, because a game that plays in CI has nobody to
// ask. Build the cheapest thing not yet built, but never spend the ward down to where it
// cannot pay its Sparks — an unpaid Spark leaves, and a ward with no Sparks earns nothing.
static int commission(gyre_t *g) {
	// She keeps enough back to pay everyone for a cycle. A ward that cannot pay its Sparks
	// loses them, and a ward with no Sparks earns nothing and owes the same.
	const int reserve = 25 * g->nsparks;
	for (int i = 0; i < NVENUES; i++) {
		char sql[256];
		snprintf(sql, sizeof sql, "SELECT built FROM venue WHERE id = %d", i);
		if (scalar(g->ward, sql) != 0) continue;
		if (g->treasury < VENUES[i].cost + reserve) continue;

		g->treasury -= VENUES[i].cost;
		// It leaves the ward. Somebody outside built the thing, and the scrip went with
		// them: a venue is not a place to keep money, it is money turned into a place.
		g->spent += VENUES[i].cost;
		snprintf(sql, sizeof sql, "UPDATE venue SET built = %d WHERE id = %d", g->cycle, i);
		if (run(g->ward, sql)) return -1;
		snprintf(sql, sizeof sql,
		         "INSERT INTO ledger(cycle, what, amount, spark) VALUES (%d, 'commission', %d, -1)",
		         g->cycle, -VENUES[i].cost);
		if (run(g->ward, sql)) return -1;
		return i;
	}
	return -1;
}

static int built(gyre_t *g, int venue) {
	char sql[128];
	snprintf(sql, sizeof sql, "SELECT built FROM venue WHERE id = %d", venue);
	return scalar(g->ward, sql) != 0;
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

// ── Paying a Spark ────────────────────────────────────────────────────────────
//
// The ward's treasury and the Spark's purse are two databases, so this is one event that
// has to land in both or in neither. That is the parallel commit protocol, and the game
// reaches it by paying somebody rather than by a test reaching for it deliberately.
//
// Each participant is an explicit SQLite transaction. Autocommit would let the first write
// commit on its own at xSync, and then the group would only contain whatever had not been
// written yet.
static int pay(gyre_t *g, spark_t *s, int amount, int item, const char *kind) {
	unsigned long long txn = 0;
	char sql[256];

	if (weft_txn_begin(&txn) != SQLITE_OK) return 1;
	if (weft_txn_join(g->ward, txn) != SQLITE_OK) goto give_up;
	if (weft_txn_join(s->db, txn) != SQLITE_OK) goto give_up;

	if (run(g->ward, "BEGIN")) goto give_up;
	if (run(s->db, "BEGIN")) goto give_up;

	snprintf(sql, sizeof sql,
	         "INSERT INTO ledger(cycle, what, amount, spark) VALUES (%d, 'wages', %d, %d)",
	         g->cycle, -amount, s->id);
	if (run(g->ward, sql)) goto give_up;

	snprintf(sql, sizeof sql, "UPDATE spark SET purse = purse + %d WHERE id = %d", amount,
	         s->id);
	if (run(s->db, sql)) goto give_up;

	if (item > 0) {
		snprintf(sql, sizeof sql, "INSERT INTO held VALUES (%d, '%s', %d)", item, kind,
		         g->cycle);
		if (run(s->db, sql)) goto give_up;
	}

	if (run(g->ward, "COMMIT")) goto give_up;
	if (run(s->db, "COMMIT")) goto give_up;

	if (weft_txn_commit(txn) != SQLITE_OK) return 1;

	g->treasury -= amount;
	s->purse += amount;
	return 0;

give_up:
	sqlite3_exec(g->ward, "ROLLBACK", NULL, NULL, NULL);
	sqlite3_exec(s->db, "ROLLBACK", NULL, NULL, NULL);
	weft_txn_abort(txn);
	return 1;
}

// ── One cycle ─────────────────────────────────────────────────────────────────

static int cycle(gyre_t *g) {
	char sql[256];
	g->cycle++;

	if (commission(g) < 0) { /* nothing affordable this cycle, which is most of them */ }
	if (post_board(g)) return 1;

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
		int earned = 0, item = 0;
		if (won) {
			earned = payout;
			g->treasury += earned;
			g->issued += earned;
			item = g->next_item++;
			snprintf(sql, sizeof sql,
			         "INSERT INTO ledger(cycle, what, amount, spark) VALUES (%d,'salvage',%d,%d)",
			         g->cycle, earned, s->id);
			if (run(g->ward, sql)) return 1;
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
		if (run(g->ward, sql)) return 1;

		// A Spark is paid a share whether or not the contract kept, because a ward that only
		// pays for success does not keep its Sparks for long. The share of nothing is
		// nothing, so a failed cycle still costs them.
		const int wage = won ? earned / 2 : 4;
		if (wage > 0 && g->treasury >= wage) {
			if (pay(g, s, wage, item, won ? "salvage" : NULL)) return 1;
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

		snprintf(sql, sizeof sql,
		         "UPDATE spark SET wear = %d, cycles = %d, x = %" PRId64 ", y = %" PRId64
		         ", z = %" PRId64 " WHERE id = %d",
		         s->wear, g->cycle, s->at.x, s->at.y, s->at.z, s->id);
		if (run(s->db, sql)) return 1;
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
		if (run(g->ward, sql)) return 1;
	}

	snprintf(sql, sizeof sql,
	         "UPDATE ward SET cycle=%d, treasury=%d, debt=%d, issued=%d, retired=%d",
	         g->cycle, g->treasury, g->debt, g->issued, g->retired);
	return run(g->ward, sql);
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
static int honest(gyre_t *g, const char *when) {
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
	if (items > g->next_item - 1) {
		printf("BROKEN at %s: %d items held, only %d ever found\n", when, items,
		       g->next_item - 1);
		return 1;
	}
	return 0;
}

static void chronicle(gyre_t *g) {
	printf("\n  cycle %d of the ward\n", g->cycle);
	printf("  treasury %d   debt %d   paid to the clock %d   built with %d   issued %d\n",
	       g->treasury, g->debt, g->retired, g->spent, g->issued);
	printf("  venues:");
	for (int i = 0; i < NVENUES; i++) {
		if (built(g, i)) printf(" %s;", VENUES[i].name);
	}
	printf("\n  sparks: ");
	for (int i = 0; i < g->nsparks && i < 8; i++) {
		printf("[%d purse %d wear %d] ", i, g->sparks[i].purse, g->sparks[i].wear);
	}
	printf("\n");
}

static void close_ward(gyre_t *g) {
	for (int i = 0; i < g->nsparks; i++) sqlite3_close(g->sparks[i].db);
	sqlite3_close(g->ward);
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
	}
	return h;
}

static int play(const char *prefix, int cycles, int nsparks, uint64_t seed, gyre_t *out) {
	gyre_t g;
	memset(&g, 0, sizeof g);
	if (found_ward(&g, prefix, nsparks, seed)) return 1;

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

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: queen play|check <cycles> [seed] [sparks]\n");
		return 2;
	}
	const char *mode = argv[1];
	const int cycles = atoi(argv[2]);
	const uint64_t seed = (argc > 3) ? strtoull(argv[3], NULL, 10) : 20260811;
	const int nsparks = (argc > 4) ? atoi(argv[4]) : 8;

	// One ward is one service, and one service is what one subscriber's slice can carry. Past
	// that the answer is a second ward on its own machine, joined underneath by FoundationDB,
	// rather than a bigger slice: the pages are already shared, so a Spark moves between wards
	// with no copy and no restore. Refusing here is the point. A ward that quietly exceeded
	// the budget would drop its venues out of every slice and look healthy doing it.
	if (nsparks > SPARKS_PER_WARD) {
		fprintf(stderr,
		        "%d Sparks is more than one ward may hold. A subscriber's slice is %d"
		        " entities, and %d of those are the venues, the Queen and a full board,"
		        " so a ward holds %d Sparks. Found a second ward and migrate the rest.\n",
		        nsparks, SLICE_ENTITIES, FIXED_ENTITIES, SPARKS_PER_WARD);
		return 2;
	}

	if (weft_fdb_start(getenv("WEFT_FDB_CLUSTER_FILE"))) {
		fprintf(stderr, "the Gyre has no FoundationDB to run on\n");
		return 1;
	}
	weft_vfs_register(1);

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
