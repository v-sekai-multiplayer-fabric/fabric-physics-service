// Freeze on settle: a body that has stopped moving becomes part of the world.
//
// Under "only things in the physics engine exist" a built object cannot be handed to some
// other system once it comes to rest -- there is nowhere else for it to be. So it stays in
// the engine and stops being simulated instead: the body is deleted and an identical geom
// is added to the worldbody at the pose the body had reached.
//
// It is still there. It still collides with anything dynamic. You can still walk into it.
// What it stops doing is having a solve run for it, and -- the part that actually pays --
// being tested against every other settled thing, because MuJoCo never generates contacts
// between two static geoms.
//
// Measured on this desk before any of this was written:
//
//     320 fresh pieces being drawn      20.12 ms   40% of a 50 ms tick
//     the same pieces settled            0.13 ms
//     the same pieces frozen             0.01 ms
//     40,000 pieces frozen over months   0.34 ms    1% of a tick
//
// So a built world is affordable if and only if built things stop being dynamic, and the
// cost of a session tracks what is being drawn right now rather than everything that was
// ever drawn. That is the whole reason this file exists.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef WEFT_FREEZE_H
#define WEFT_FREEZE_H

#include <mujoco/mujoco.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// How still, and for how long, before a body is considered finished.
//
// The speed threshold is per-body linear speed in metres a second. The tick count is how
// many consecutive ticks it must hold. Both are deliberately loose: freezing a body that
// was about to move again is a visible wrong answer, and the cost of waiting is one more
// tick of simulating something that was already asleep.
typedef struct {
	mjtNum still_speed;   // default 0.01 m/s
	int    still_ticks;   // default 30, which is 1.5 s at 20 Hz
	int    max_per_tick;  // freeze at most this many in one recompile, default 64
} weft_freeze_cfg;

weft_freeze_cfg weft_freeze_defaults(void);

// What separates an identity from its geom index in a frozen name: "crate7#0", "crate7#1".
// The reverse transfer finds every piece of one thing by this prefix.
#define WEFT_FROZEN_SEP "#"

// Per-body bookkeeping. Sized to the model at init and reallocated on recompile.
typedef struct {
	int  *still_for;      // consecutive ticks each body has been still
	int   nbody;
	long  frozen_total;   // how many bodies have been promoted, all time
	long  recompiles;     // how many times the model was rebuilt
	double last_ms;       // wall clock of the last recompile
} weft_freeze_t;

// `spec` must be the spec `m` was compiled from; it is edited in place.
int  weft_freeze_init(weft_freeze_t *f, const mjModel *m);
void weft_freeze_close(weft_freeze_t *f);

// Call once a tick, after mj_step. Returns the number of bodies frozen this tick, which is
// zero on most ticks. When it is non-zero the model has been recompiled and `m`/`d` now
// point at the new one -- any cached ids are stale.
//
// Bodies whose name begins with `keep_prefix` are never frozen. That is how avatars stay
// dynamic: a skeleton is not scenery, however still it is holding.
int weft_freeze_tick(weft_freeze_t *f, mjSpec *spec, mjModel **m, mjData **d,
                     const weft_freeze_cfg *cfg, const char *keep_prefix);

// World back to dynamic: the same transfer with the endpoints swapped, for when somebody
// grabs a thing they built. Returns how many geoms came back, 0 if that identity is not
// frozen here. Recompiles, so `m`/`d` are rebuilt and cached ids are stale.
//
// Without this, freezing is a one-way ratchet and a built world can only calcify.
int weft_thaw(weft_freeze_t *f, mjSpec *spec, mjModel **m, mjData **d, const char *name);

#ifdef __cplusplus
}
#endif

#endif  // WEFT_FREEZE_H
