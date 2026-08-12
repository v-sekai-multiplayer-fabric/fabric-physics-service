// The fan-out edge, reachable from C. See fanout_bridge.h.
//
// SPDX-License-Identifier: Apache-2.0

#include "fanout_bridge.h"

#include "fanout.h"

namespace {

// What a transport would have been handed. The edge writes one batch per subscriber per tick
// and the receiver recovers the count as `len / 100`, so a byte total is the honest measure of
// what this tick costs a NIC — counting entities would hide that a slice is a single write.
void count_only(void *subscriber, const uint8_t *buf, size_t len) {
	(void)buf;
	*static_cast<size_t *>(subscriber) += len;
}

} // namespace

extern "C" size_t bench_slice_cap(void) { return MAX_SLICE_ENTITIES; }

extern "C" size_t bench_fanout_one(const int64_t interest[6], const xr_grid_entity_packet_t *ents,
                                   size_t n, size_t *bytes) {
	size_t sent_bytes = 0;
	subscriber_t sub;

	sub.interest.min_x = interest[0];
	sub.interest.max_x = interest[1];
	sub.interest.min_y = interest[2];
	sub.interest.max_y = interest[3];
	sub.interest.min_z = interest[4];
	sub.interest.max_z = interest[5];
	sub.conn = &sent_bytes;
	sub.send = count_only;

	size_t out = fanout_one(sub, ents, n);
	if (bytes) *bytes += sent_bytes;
	return out;
}
