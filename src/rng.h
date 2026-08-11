// ChaCha20 as a seeded stream of numbers, so a ward replays exactly.
//
// The Gyre's own guest uses ChaCha20 for the same reason, and records the choice as its one
// documented deviation from the scenario it ports. This follows it rather than picking a
// second generator: two games in one fabric disagreeing about randomness is a difference
// nobody can account for later.
//
// Determinism is not decoration here. A cycle that cannot be replayed cannot be checked,
// and every invariant in this game is checked by playing the same seed twice and comparing.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef GYRE_RNG_H
#define GYRE_RNG_H

#include <stdint.h>
#include <string.h>

typedef struct {
	uint32_t state[16];
	uint32_t out[16];
	int used; // words of `out` already handed out
} rng_t;

#define RNG_ROTL(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define RNG_QR(a, b, c, d)                                                                   \
	a += b, d ^= a, d = RNG_ROTL(d, 16), c += d, b ^= c, b = RNG_ROTL(b, 12), a += b,        \
	d ^= a, d = RNG_ROTL(d, 8), c += d, b ^= c, b = RNG_ROTL(b, 7)

static inline void rng_block(rng_t *r) {
	uint32_t x[16];
	memcpy(x, r->state, sizeof x);
	for (int i = 0; i < 10; i++) {
		RNG_QR(x[0], x[4], x[8], x[12]);
		RNG_QR(x[1], x[5], x[9], x[13]);
		RNG_QR(x[2], x[6], x[10], x[14]);
		RNG_QR(x[3], x[7], x[11], x[15]);
		RNG_QR(x[0], x[5], x[10], x[15]);
		RNG_QR(x[1], x[6], x[11], x[12]);
		RNG_QR(x[2], x[7], x[8], x[13]);
		RNG_QR(x[3], x[4], x[9], x[14]);
	}
	for (int i = 0; i < 16; i++) r->out[i] = x[i] + r->state[i];
	if (++r->state[12] == 0) r->state[13]++;
	r->used = 0;
}

// One seed makes one ward. The constants are ChaCha's own, and the counter starts at zero,
// so the same seed replays from the beginning every time.
static inline void rng_seed(rng_t *r, uint64_t seed) {
	static const char sigma[] = "expand 32-byte k";
	memcpy(r->state, sigma, 16);
	for (int i = 4; i < 12; i++) r->state[i] = (uint32_t)(seed >> ((i % 2) * 32)) ^ (uint32_t)i;
	r->state[12] = 0;
	r->state[13] = 0;
	r->state[14] = 0x67452301u;
	r->state[15] = 0xefcdab89u;
	rng_block(r);
}

static inline uint32_t rng_next(rng_t *r) {
	if (r->used >= 16) rng_block(r);
	return r->out[r->used++];
}

// A number below `n`. Rejection rather than a modulo, so the low values are not commoner
// than the high ones — a bias here would be a thumb on the scale of every contract.
static inline uint32_t rng_below(rng_t *r, uint32_t n) {
	if (n == 0) return 0;
	const uint32_t limit = 0xFFFFFFFFu - (0xFFFFFFFFu % n) - 1;
	uint32_t v;
	do {
		v = rng_next(r);
	} while (v > limit);
	return v % n;
}

#endif
