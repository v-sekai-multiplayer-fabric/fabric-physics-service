// Property tests for the ward's names and places.
//
// These are unit tests and only unit tests. Nothing here opens a database, a socket or a
// cluster — the game itself is checked by playing it, which is what `queen check` and CI do,
// and a second copy of that here would be a worse version of a test that already exists.
//
// What is worth testing on its own is the arithmetic under `src/wire.h`: a wire id is a primary
// key, and its layout has to be exact for every input rather than for the handful this ward
// happened to hand out. A property says that where an example cannot.
//
// SPDX-License-Identifier: Apache-2.0

#include "wire.h"

#include <string>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace {

// ── Wire ids ──────────────────────────────────────────────────────────────────

// The claim in `wire.h` is that packing is exact rather than probable. Exact means the pack is
// injective, and injective means the two halves come back out.
void WireIdKeepsBothHalves(int ward, uint32_t local) {
	const uint32_t id = wire_id(ward, local);
	EXPECT_EQ(id >> WIRE_LOCAL_BITS, static_cast<uint32_t>(ward));
	EXPECT_EQ(id & (WIRE_LOCAL_MAX - 1), local);
}
FUZZ_TEST(WireId, WireIdKeepsBothHalves)
    .WithDomains(fuzztest::InRange(0, static_cast<int>(WIRE_WARD_MAX) - 1),
                 fuzztest::InRange(0u, WIRE_LOCAL_MAX - 1));

// `wire_id` exits rather than wrapping, so the domains above stop at the limit on purpose: the
// refusal is the behaviour, and a test that triggered it would kill the runner rather than
// report. What can be checked without dying is that the limit is where the header says it is.
TEST(WireId, RefusesNothingBelowTheLimit) {
	EXPECT_EQ(wire_id(WIRE_WARD_MAX - 1, WIRE_LOCAL_MAX - 1), 0xffffffffu);
	EXPECT_EQ(wire_id(0, 0), 0u);
}

// One ward's entities share one local space, which is the reason the classes take fixed bases.
// A venue, a Spark and a contract in the same ward must never arrive under one name.
void ClassesDoNotOverlap(int ward, int venue, int spark, int contract) {
	const uint32_t v = wire_id(ward, WIRE_VENUE_BASE + venue);
	const uint32_t s = wire_id(ward, WIRE_SPARK_BASE + spark);
	const uint32_t c = wire_id(ward, WIRE_CONTRACT_BASE + contract);
	const uint32_t q = wire_id(ward, WIRE_QUEEN);
	EXPECT_NE(v, s);
	EXPECT_NE(v, c);
	EXPECT_NE(s, c);
	EXPECT_NE(q, v);
	EXPECT_NE(q, s);
	EXPECT_NE(q, c);
}
FUZZ_TEST(WireId, ClassesDoNotOverlap)
    .WithDomains(fuzztest::InRange(0, static_cast<int>(WIRE_WARD_MAX) - 1),
                 // A venue index, a Spark id and a board row, each held to the room its base
                 // leaves before the next one starts.
                 fuzztest::InRange(0, static_cast<int>(WIRE_SPARK_BASE - WIRE_VENUE_BASE) - 1),
                 fuzztest::InRange(0, static_cast<int>(WIRE_CONTRACT_BASE - WIRE_SPARK_BASE) - 1),
                 fuzztest::InRange(0, static_cast<int>(WIRE_LOCAL_MAX - WIRE_CONTRACT_BASE) - 1));

// Two wards never share a name, whatever their locals are. This is the property that lets
// `shard` run eight wards and still assert that no two entities share a wire id.
void WardsDoNotOverlap(int a, int b, uint32_t la, uint32_t lb) {
	if (a == b) return;
	EXPECT_NE(wire_id(a, la), wire_id(b, lb));
}
FUZZ_TEST(WireId, WardsDoNotOverlap)
    .WithDomains(fuzztest::InRange(0, static_cast<int>(WIRE_WARD_MAX) - 1),
                 fuzztest::InRange(0, static_cast<int>(WIRE_WARD_MAX) - 1),
                 fuzztest::InRange(0u, WIRE_LOCAL_MAX - 1),
                 fuzztest::InRange(0u, WIRE_LOCAL_MAX - 1));

// `class_owner` is the packet's own idiom, `(class << 24) | owner`, and a subscriber splits it
// back apart. So it has to survive the round trip for every ward the wire id allows.
void ClassOwnerKeepsBothHalves(int cls, int ward) {
	const uint32_t co = class_owner(cls, ward);
	EXPECT_EQ(co >> 24, static_cast<uint32_t>(cls));
	EXPECT_EQ(co & 0xffffffu, static_cast<uint32_t>(ward));
}
FUZZ_TEST(WireId, ClassOwnerKeepsBothHalves)
    .WithDomains(fuzztest::ElementOf<int>({CLASS_SPARK, CLASS_VENUE, CLASS_CONTRACT, CLASS_QUEEN}),
                 fuzztest::InRange(0, static_cast<int>(WIRE_WARD_MAX) - 1));

// ── Names ─────────────────────────────────────────────────────────────────────

// `mix64` is where a UUID's distinctness comes from: distinct facts have to reach distinct
// hashes, or two entities get one name. Splitmix64's finaliser is invertible, and this says so
// in the form the caller relies on.
void Mix64IsInjective(uint64_t a, uint64_t b) {
	if (a == b) return;
	EXPECT_NE(mix64(a), mix64(b));
}
FUZZ_TEST(Uuid8, Mix64IsInjective);

static bool IsHex(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// A name that is not a UUID is a name some other reader will reject, so the shape is checked
// for every input rather than for the one that was printed during a play.
void Uuid8HasTheShape(uint64_t seed, int ward, int cls, int cycle, uint64_t seq) {
	char out[37];
	uuid8(out, seed, ward, cls, cycle, seq);
	const std::string s(out);

	ASSERT_EQ(s.size(), 36u);
	for (size_t i = 0; i < s.size(); i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23)
			EXPECT_EQ(s[i], '-') << "at " << i;
		else
			EXPECT_TRUE(IsHex(s[i])) << "at " << i << " in " << s;
	}
	// RFC 9562: the version nibble, and a variant of 0b10xx. The game needs version 8 because
	// v7's first 48 bits are the wall clock and this ward replays from a seed instead.
	EXPECT_EQ(s[14], '8');
	EXPECT_TRUE(s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b') << s;
}
FUZZ_TEST(Uuid8, Uuid8HasTheShape)
    .WithDomains(fuzztest::Arbitrary<uint64_t>(),
                 fuzztest::InRange(0, static_cast<int>(WIRE_WARD_MAX) - 1),
                 fuzztest::ElementOf<int>({CLASS_SPARK, CLASS_VENUE, CLASS_CONTRACT, CLASS_QUEEN}),
                 fuzztest::InRange(0, 1 << 20), fuzztest::Arbitrary<uint64_t>());

// One seed makes one ward, and that starts with the names. Two calls with the same facts have
// to agree, or `check` compares two fingerprints that were never going to match.
void Uuid8Replays(uint64_t seed, int ward, int cls, int cycle, uint64_t seq) {
	char a[37], b[37];
	uuid8(a, seed, ward, cls, cycle, seq);
	uuid8(b, seed, ward, cls, cycle, seq);
	EXPECT_STREQ(a, b);
}
FUZZ_TEST(Uuid8, Uuid8Replays)
    .WithDomains(fuzztest::Arbitrary<uint64_t>(),
                 fuzztest::InRange(0, static_cast<int>(WIRE_WARD_MAX) - 1),
                 fuzztest::ElementOf<int>({CLASS_SPARK, CLASS_VENUE, CLASS_CONTRACT, CLASS_QUEEN}),
                 fuzztest::InRange(0, 1 << 20), fuzztest::Arbitrary<uint64_t>());

// The high 48 bits are the cycle, which is what makes the ids sort in the order things
// happened. A later cycle must never name something that sorts before an earlier one.
void Uuid8SortsByCycle(uint64_t seed, uint64_t seq, int early, int late) {
	if (early >= late) return;
	char a[37], b[37];
	uuid8(a, seed, 0, CLASS_SPARK, early, seq);
	uuid8(b, seed, 0, CLASS_SPARK, late, seq);
	EXPECT_LT(std::string(a).substr(0, 13), std::string(b).substr(0, 13));
}
FUZZ_TEST(Uuid8, Uuid8SortsByCycle)
    .WithDomains(fuzztest::Arbitrary<uint64_t>(), fuzztest::Arbitrary<uint64_t>(),
                 fuzztest::InRange(0, 1 << 20), fuzztest::InRange(0, 1 << 20));

// ── Quoting ───────────────────────────────────────────────────────────────────

// The venues are called things like Cycle's End Tavern, so this runs on every name the ward
// writes into SQL. What it must never do is leave an odd number of apostrophes behind: that is
// not a truncated name, it is an unterminated string literal and the statement after it.
void QuotedDoublesEveryApostrophe(const std::string &in, int cap) {
	// `cap` starts at one because the caller always passes a buffer: `quoted` writes its
	// terminator before it checks anything, so a zero-length buffer is a precondition and not a
	// case. Every call site passes `sizeof buf`.
	std::vector<char> out(static_cast<size_t>(cap), '\x7f');
	quoted(in.c_str(), out.data(), out.size());

	const std::string got(out.data());
	ASSERT_LT(got.size(), out.size()); // terminated inside the buffer it was given

	size_t apostrophes = 0;
	for (size_t i = 0; i < got.size(); i++)
		if (got[i] == '\'') apostrophes++;
	EXPECT_EQ(apostrophes % 2, 0u) << "an odd quote closes the literal early: " << got;
}
FUZZ_TEST(Quoted, QuotedDoublesEveryApostrophe)
    .WithDomains(fuzztest::Arbitrary<std::string>(), fuzztest::InRange(1, 512));

// With room to spare the name comes back exactly, which is the other half: escaping must not
// change what the venue is called.
void QuotedKeepsTheName(const std::string &in) {
	if (in.find('\0') != std::string::npos) return;
	std::vector<char> out(in.size() * 2 + 2, '\0');
	quoted(in.c_str(), out.data(), out.size());

	std::string back;
	for (const char *p = out.data(); *p; p++) {
		back.push_back(*p);
		if (*p == '\'') p++; // one of the pair
	}
	EXPECT_EQ(back, in);
}
FUZZ_TEST(Quoted, QuotedKeepsTheName);

// ── Places ────────────────────────────────────────────────────────────────────
//
// The ids are held to the ranges the game issues. Past those the arithmetic overflows, and a
// test that fed it a billion would be reporting on C rather than on the ward.

// Every Spark without a contract stands on the Commons deck, inside the box the interest
// filter compares against. A Spark placed outside it is a Spark a subscriber never sees.
void SparkHomeIsOnTheCommons(int id) {
	const place_t p = spark_home(id);
	EXPECT_EQ(p.y, COMMONS_UM);
	EXPECT_GE(p.x, UM(-25));
	EXPECT_LE(p.x, UM(25));
	EXPECT_GE(p.z, UM(-25));
	EXPECT_LE(p.z, UM(25));
}
FUZZ_TEST(Places, SparkHomeIsOnTheCommons)
    .WithDomains(fuzztest::InRange(0, static_cast<int>(WIRE_CONTRACT_BASE - WIRE_SPARK_BASE) - 1));

// Half the work is down in the Under-Market, and which half is decided by the row rather than
// by a draw — so the board replays with the contracts in the same places.
void ContractPlaceIsOnADeck(int id) {
	const place_t p = contract_place(id);
	EXPECT_EQ(p.y, (id % 2) ? UNDER_UM : COMMONS_UM);
	EXPECT_GE(p.x, UM(-50));
	EXPECT_LE(p.x, UM(50));
	EXPECT_GE(p.z, UM(-50));
	EXPECT_LE(p.z, UM(50));
}
FUZZ_TEST(Places, ContractPlaceIsOnADeck)
    .WithDomains(fuzztest::InRange(0, static_cast<int>(WIRE_LOCAL_MAX - WIRE_CONTRACT_BASE) - 1));

} // namespace
