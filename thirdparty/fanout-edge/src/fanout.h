/*
 * fanout.h -- what a caller of the fan-out edge needs to know.
 *
 * `subscriber_t` and `fanout_one` were declared inside fanout.cpp, which meant nothing outside
 * that translation unit could call them: the interest filter was written, was correct, and was
 * unreachable. A benchmark measuring what a zone tick costs cannot time the real fan-out
 * without this file, and would otherwise reimplement the selection and time an algorithm the
 * fabric does not run.
 *
 * The delivery seam stays a function pointer, so a caller with no transport at all -- a test, a
 * benchmark -- passes a counter and gets the real filtering.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef FABRIC_FANOUT_H
#define FABRIC_FANOUT_H

#include <cstddef>
#include <cstdint>

#include "predictive_bvh.h"
extern "C" {
#include "gen/xr_grid_entity_packet.h"
}

/*
 * Delivery seam. WebTransport is not in the container yet, so a subscriber's bytes go through
 * this function pointer. The default is an ordinary h2o send; swap it for a WebTransport
 * datagram sink later without touching the interest logic.
 */
typedef void (*fanout_sink_t)(void *subscriber, const uint8_t *buf, size_t len);

struct subscriber_t {
	Aabb interest;    /* the box this subscriber cares about, in micrometres */
	void *conn;       /* opaque delivery handle (an h2o generator, a WT session) */
	fanout_sink_t send;
};

/* Cap one subscriber's slice so a single write stays inside a datagram-sized batch.
 * 64 entities * 100 bytes = 6400 bytes, comfortably inside one message. */
#define MAX_SLICE_ENTITIES 64

/* Fan one tick of the zone out to one subscriber: filter by interest, pack the survivors
 * back-to-back, send once. Returns the number of entities sent. */
size_t fanout_one(const subscriber_t &sub, const xr_grid_entity_packet_t *ents, size_t n);

/* Fan the whole zone out to every subscriber of this relay leaf. Called once per tick, after
 * the authority tick has produced the current entity set. */
void fanout_tick(const subscriber_t *subs, size_t sub_count,
                 const xr_grid_entity_packet_t *ents, size_t n);

#endif
