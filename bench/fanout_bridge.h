// The fan-out edge, reachable from C.
//
// `fanout_one` takes a `const subscriber_t &`, so it is C++ and the benchmark is C. This is the
// three lines that let one call the other, and nothing more: no selection, no packing, no
// second opinion about interest. Reimplementing any of that here is exactly what vendoring the
// edge was meant to avoid.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef BENCH_FANOUT_BRIDGE_H
#define BENCH_FANOUT_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#include "gen/xr_grid_entity_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

// One subscriber's slice, through the real filter. `interest` is min/max per axis in int64
// micrometres, the order `Aabb` holds them. Returns the entities sent; `bytes` takes what the
// sink was handed, which is the number a NIC would have to carry.
//
// The sink counts rather than sends. That is the point of `fanout_sink_t` being a pointer: a
// caller with no transport still gets the real filtering, and what is being timed here is the
// filtering and the packing, not a socket.
size_t bench_fanout_one(const int64_t interest[6], const xr_grid_entity_packet_t *ents, size_t n,
                        size_t *bytes);

// `MAX_SLICE_ENTITIES`, reached from C. It lives in `fanout.h`, which is C++ because
// `predictive_bvh.h` is, so a C caller cannot include it — and a C caller that redefined the
// number would be the second copy of the one constant this whole file exists to avoid copying.
size_t bench_slice_cap(void);

#ifdef __cplusplus
}
#endif

#endif
