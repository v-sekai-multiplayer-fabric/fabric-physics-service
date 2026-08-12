// Property tests for the ward's randomness.
//
// One seed makes one ward. Every invariant this game has is checked by playing a seed twice and
// comparing a fingerprint, so a generator that drifted would not fail loudly — it would make
// `check` fail and say nothing about why. These say what the generator owes that check.
//
// SPDX-License-Identifier: Apache-2.0

#include "rng.h"

#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace {

std::vector<uint32_t> Draw(uint64_t seed, int n) {
	rng_t r;
	rng_seed(&r, seed);
	std::vector<uint32_t> v;
	v.reserve(static_cast<size_t>(n));
	for (int i = 0; i < n; i++) v.push_back(rng_next(&r));
	return v;
}

// The whole replay check rests on this. Two runs of one seed draw the same numbers, from the
// first one to well past the block boundary where `rng_block` refills.
void SameSeedSameStream(uint64_t seed) { EXPECT_EQ(Draw(seed, 200), Draw(seed, 200)); }
FUZZ_TEST(Rng, SameSeedSameStream);

// A stream does not depend on how it was asked for. `rng_next` refills every sixteenth draw,
// and a caller that stopped and resumed across that seam must not see a different game.
void ChunkingDoesNotMatter(uint64_t seed, int split) {
	rng_t r;
	rng_seed(&r, seed);
	std::vector<uint32_t> piecewise;
	for (int i = 0; i < split; i++) piecewise.push_back(rng_next(&r));
	for (int i = split; i < 64; i++) piecewise.push_back(rng_next(&r));
	EXPECT_EQ(piecewise, Draw(seed, 64));
}
FUZZ_TEST(Rng, ChunkingDoesNotMatter).WithDomains(fuzztest::Arbitrary<uint64_t>(),
                                                  fuzztest::InRange(0, 64));

// The contract of `rng_below`: a number below `n`, for every `n` — including the zero that a
// caller reaches when a list it is choosing from turned out to be empty.
void BelowStaysBelow(uint64_t seed, uint32_t n) {
	rng_t r;
	rng_seed(&r, seed);
	for (int i = 0; i < 64; i++) {
		const uint32_t v = rng_below(&r, n);
		if (n == 0)
			EXPECT_EQ(v, 0u);
		else
			EXPECT_LT(v, n);
	}
}
FUZZ_TEST(Rng, BelowStaysBelow);

// Rejection rather than a modulo, so the low values are not commoner than the high ones. A bias
// here would be a thumb on the scale of every contract, and it would be invisible: the game
// would still replay, it would just quietly favour whichever outcome came first in the list.
//
// The bound is loose on purpose. This is a seeded generator, so the test is deterministic for
// any seed it is given, and the margin is there to leave the properties above as the sharp ones.
void NoBucketIsStarved(uint64_t seed, uint32_t n) {
	rng_t r;
	rng_seed(&r, seed);
	std::vector<int> hits(n, 0);
	const int draws = 200 * static_cast<int>(n);
	for (int i = 0; i < draws; i++) hits[rng_below(&r, n)]++;
	for (uint32_t i = 0; i < n; i++)
		EXPECT_GT(hits[i], draws / static_cast<int>(n) / 2) << "value " << i << " of " << n;
}
FUZZ_TEST(Rng, NoBucketIsStarved)
    .WithDomains(fuzztest::Arbitrary<uint64_t>(), fuzztest::InRange(2u, 20u));

// The counter starts at zero and the state is set from the seed alone, so a ward founded on one
// machine is the ward founded on another. Nothing here reads a clock, an address or a PID —
// which is the one thing that would make a seeded game unreproducible without ever failing.
TEST(Rng, SeedIsTheWholeState) {
	rng_t a, b;
	rng_seed(&a, 20260811);
	rng_seed(&b, 20260811);
	EXPECT_EQ(0, memcmp(&a, &b, sizeof a));
}

} // namespace
