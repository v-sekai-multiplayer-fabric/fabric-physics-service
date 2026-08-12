// The one predictive-BVH helper a caller has to supply.
//
// `predictive_bvh.h` is emitted from Lean and says so itself: the non-polynomial helpers are
// forward-declared there and "definitions live in the adapter", because the tree code is
// generated and the primitives are not. `fabric-godot-core` has an adapter, and it is
// Godot-shaped — `Vector3`, `RefCounted`, the engine's allocator. A benchmark that pulled it in
// would be linking an engine to compare six integers.
//
// So this is the adapter, and it is only what `fanout.cpp` actually calls.
//
// Isolation beats duplication. Vendoring the engine's adapter would be duplication — a second
// copy of twenty helpers, nineteen of which nothing here calls, each one able to drift from the
// original without any consumer noticing. Six comparisons written against a declaration the
// generated header publishes is isolation: this file depends on `predictive_bvh.h` and on
// nothing else, and the day the BVH changes what `aabb_overlaps` means, the declaration changes
// and this stops compiling. A duplicate would have kept building and quietly disagreed.
//
// SPDX-License-Identifier: Apache-2.0

#include "predictive_bvh.h"

// Closed intervals on all three axes, in int64 micrometres. Touching counts as overlapping:
// the boxes are already ghost-expanded by two ticks of velocity, so a subscriber who is exactly
// on the boundary is a subscriber about to be inside it, and dropping them for one tick is a
// pop-in the expansion exists to prevent.
bool aabb_overlaps(const Aabb *a, const Aabb *o) {
	return a->min_x <= o->max_x && a->max_x >= o->min_x && a->min_y <= o->max_y &&
	       a->max_y >= o->min_y && a->min_z <= o->max_z && a->max_z >= o->min_z;
}
