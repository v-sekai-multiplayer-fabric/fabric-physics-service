// The Queen's interactor: a command in, reply bytes out.
//
// It has no socket, no poll loop and no idea what carried the command here. `queen serve`
// composes it with whatever transports it was given, and `thirdparty/interactor` is the
// contract that lets it be composed with transports written before this file existed.
//
// The rules are checked here on receipt, whatever a client chose to show. A filtered menu is a
// convenience and never an authorization.
//
// SPDX-License-Identifier: Apache-2.0

#include "interactor.h"

#include "ward.h"

#include <stdio.h>
#include <string.h>

// ── The ward, as a subscriber sees it ─────────────────────────────────────────
//
// Every row carries its `wire` and `owner`, because that is how a client matches this reply to
// the `XRGridEntityPacket` for the same entity. Matching by position instead would be guessing,
// and two Sparks standing on one contract would defeat it.

static void say_sparks(weft_cbor_t *c, gyre_t *g) {
	weft_cbor_text(c, "sparks");
	weft_cbor_array(c, (uint64_t)g->nsparks);
	for (int i = 0; i < g->nsparks; i++) {
		const spark_t *s = &g->sparks[i];
		weft_cbor_map(c, 8);
		weft_cbor_kv_int(c, "id", s->id);
		weft_cbor_kv_int(c, "purse", s->purse);
		weft_cbor_kv_int(c, "wear", s->wear);
		weft_cbor_kv_int(c, "x", s->at.x);
		weft_cbor_kv_int(c, "y", s->at.y);
		weft_cbor_kv_int(c, "z", s->at.z);
		weft_cbor_kv_int(c, "wire", scalar(s->db, "SELECT wire FROM spark"));
		weft_cbor_kv_int(c, "owner", scalar(s->db, "SELECT owner FROM spark"));
	}
}

// The venues and the board come out of the ward in one statement each, so what a client is told
// is what the database holds rather than what this process remembers.
static void say_rows(weft_cbor_t *c, gyre_t *g, const char *key, const char *sql, int ncols,
                     const char *const *cols) {
	sqlite3_stmt *st;
	int n = 0;

	weft_cbor_text(c, key);
	if (sqlite3_prepare_v2(g->ward, sql, -1, &st, NULL) != SQLITE_OK) {
		weft_cbor_array(c, 0);
		return;
	}
	// The count has to precede the items in a definite-length array, and the rows only arrive
	// by stepping. Rather than a second query, write the array head as indefinite and stop it
	// with a break: RFC 8949 keeps 0x9f/0xff for exactly this, a producer streaming a sequence
	// whose length it does not know yet.
	weft_cbor_raw(c, 0x9f);
	while (sqlite3_step(st) == SQLITE_ROW) {
		weft_cbor_map(c, (uint64_t)ncols);
		for (int i = 0; i < ncols; i++) {
			weft_cbor_text(c, cols[i]);
			if (sqlite3_column_type(st, i) == SQLITE_TEXT)
				weft_cbor_text(c, (const char *)sqlite3_column_text(st, i));
			else if (sqlite3_column_type(st, i) == SQLITE_NULL)
				weft_cbor_raw(c, 0xf6);
			else
				weft_cbor_int(c, sqlite3_column_int64(st, i));
		}
		n++;
	}
	weft_cbor_break(c);
	sqlite3_finalize(st);
	(void)n;
}

// The ward scalars. `/commission` changes a venue and the treasury together, and no interest
// box can ever carry the second: `treasury`, `debt`, `issued`, `retired` and `spent` have no
// place, so they ride the reliable control stream instead of a slice. This is that stream.
static void say_ward(weft_cbor_t *c, gyre_t *g) {
	static const char *const VCOLS[] = {"id", "name", "cost", "built", "x", "y", "z", "wire", "owner"};
	static const char *const BCOLS[] = {"id",   "kind", "payout", "risk", "taken",
	                                    "outcome", "x",  "y",      "z",    "wire", "owner"};

	weft_cbor_text(c, "ward");
	weft_cbor_map(c, 8);
	weft_cbor_kv_int(c, "cycle", g->cycle);
	weft_cbor_kv_int(c, "treasury", g->treasury);
	weft_cbor_kv_int(c, "debt", g->debt);
	weft_cbor_kv_int(c, "issued", g->issued);
	weft_cbor_kv_int(c, "retired", g->retired);
	weft_cbor_kv_int(c, "spent", g->spent);
	weft_cbor_kv_int(c, "sparks", g->nsparks);
	weft_cbor_kv_int(c, "room", ward_room(g));

	say_rows(c, g, "venues",
	         "SELECT id, name, cost, built, x, y, z, wire, owner FROM venue ORDER BY id", 9, VCOLS);
	say_rows(c, g, "board",
	         "SELECT id, kind, payout, risk, taken, outcome, x, y, z, wire, owner FROM board"
	         " ORDER BY id",
	         11, BCOLS);
	say_sparks(c, g);
}

// ── Commands ──────────────────────────────────────────────────────────────────
//
// A filtered menu is a convenience and never an authorization, so the server checks on receipt
// whatever the client chose to show. What it can check today is the game's own rules — a purse
// the treasury cannot fill, a venue already standing, a Spark who is not here. The rebac
// relations that decide who may run `/restart` at all are not wired to this process yet, and
// pretending otherwise here would be the exact mistake the RFD warns about.
static int ward_command(gyre_t *g, char *line, weft_cbor_t *c, int *stop) {
	char *verb = line, *arg1 = NULL, *arg2 = NULL;
	int ok = 1;
	const char *say = "";

	while (*verb == '/' || *verb == ' ') verb++;
	for (char *p = verb; *p; p++) {
		if (*p != ' ') continue;
		*p = '\0';
		if (!arg1) arg1 = p + 1;
		else if (!arg2) { arg2 = p + 1; break; }
	}

	if (!strcmp(verb, "look") || !*verb) {
		// A read changes no entity, so the interest filter never runs and this reaches the
		// caller alone. It is the one command with no reach at all.
		say = "the ward as it stands";
	} else if (!strcmp(verb, "cycle")) {
		int n = arg1 ? atoi(arg1) : 1;
		if (n < 1) n = 1;
		if (n > 64) n = 64; // a command is not a way to run a thousand cycles inside one poll
		for (int i = 0; i < n; i++) {
			if (cycle(g)) { ok = 0; say = "the ward stopped mid-cycle"; break; }
			if (honest(g, "a cycle")) { ok = 0; say = "the ward stopped being honest"; break; }
		}
		if (ok) say = "the ward moved";
	} else if (!strcmp(verb, "commission")) {
		const int v = arg1 ? atoi(arg1) : -1;
		if (build_venue(g, v)) {
			ok = 0;
			say = "no such venue, or it is already built, or the treasury will not reach";
		} else {
			// The venue appears to every box that overlaps its place. The treasury moves with
			// it and reaches no box at all, which is why both are in this one reply.
			say = "commissioned";
		}
	} else if (!strcmp(verb, "pay")) {
		const int who = arg1 ? atoi(arg1) : -1;
		const int amount = arg2 ? atoi(arg2) : 0;
		spark_t *s = NULL;
		for (int i = 0; i < g->nsparks; i++)
			if (g->sparks[i].id == who) s = &g->sparks[i];
		if (!s || amount <= 0 || g->treasury < amount) {
			ok = 0;
			say = "no such Spark here, or an amount the treasury will not reach";
		} else if (pay(g, s, amount, NULL, NULL)) {
			ok = 0;
			say = "the payment landed on neither side";
		} else {
			say = "paid";
		}
	} else if (!strcmp(verb, "restart")) {
		// `found_ward` drops and recreates every table, so this re-founds the ward for everyone
		// at once. No interest box excludes it, and it is the command a menu should hide from a
		// player who may not run it.
		const int n = g->nsparks;
		const uint64_t seed = g->seed;
		close_ward(g);
		memset(g, 0, sizeof *g);
		if (found_ward(g, "gyre-live", n, seed, 0)) {
			*stop = 1;
			ok = 0;
			say = "the ward could not be founded again";
		} else {
			say = "the ward is founded again";
		}
	} else if (!strcmp(verb, "quit")) {
		say = "goodbye";
	} else {
		ok = 0;
		say = "no such command";
	}

	// Seven: `ok`, `say`, `honest`, and the four `say_ward` writes. A definite map that
	// undercounts is not a short reply, it is a malformed one — the client's decoder stops at
	// the declared pair and the rest of the frame becomes trailing bytes it never sees.
	weft_cbor_map(c, 7);
	weft_cbor_text(c, "ok");
	weft_cbor_bool(c, ok);
	weft_cbor_text(c, "say");
	weft_cbor_text(c, say);
	weft_cbor_text(c, "honest");
	weft_cbor_bool(c, g->ward ? !honest(g, "a command") : 0);
	say_ward(c, g);
	return !strcmp(verb, "quit");
}


// ── The contract ──────────────────────────────────────────────────────────────

static size_t ward_ask(void *ctx, const char *command, unsigned char *reply, size_t cap,
                       int *stop) {
	gyre_t *g = (gyre_t *)ctx;
	char line[WARD_COMMAND_MAX];

	// `ward_command` writes NULs into the line to split it, and the command is the transport's
	// memory until it says otherwise. So it gets a copy it may own.
	snprintf(line, sizeof line, "%s", command);

	weft_cbor_t c = weft_cbor_to(reply, cap);
	(void)ward_command(g, line, &c, stop);
	if (weft_cbor_over(&c)) {
		// A ward too big to describe in one reply is a real limit, and it is reported rather
		// than silently cut: a truncated batch decodes as a short one.
		fprintf(stderr, "the ward does not fit in %zu bytes\n", cap);
		return 0;
	}
	return c.n;
}

weft_interactor_t ward_interactor(gyre_t *g) {
	weft_interactor_t in = {ward_ask, g};
	return in;
}
