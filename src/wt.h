// WebTransport termination, in the Queen's own process.
//
// Mirrored from `thirdparty/gateway-edge/transport/`, not linked from it: that copy carries
// zone-server's tick, its `z_id` and its iceoryx2 waitset, none of which this process has.
// Mirroring is what `cmake/picoquic.cmake` already does, for the same reason.
//
// The transport itself is picoquic's: `picohttp/webtransport.c` and `h3zero_*`. Nothing here
// implements WebTransport. This is the glue between a QUIC stream and an interactor, and it
// knows nothing about what the interactor does — it includes `weft/interactor.h` and no header
// of the Queen's at all.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef QUEEN_WT_H
#define QUEEN_WT_H

#include "weft/interactor.h"

typedef struct wt_t wt_t;

// `bind_addr` is a numeric address or NULL for every interface. Fly needs the literal
// `fly-global-services` resolved here: a UDP socket bound to `::` or `0.0.0.0` there replies
// from the wrong source address and the packets are dropped on the way out.
wt_t *wt_open(int port, const char *bind_addr, const char *cert_file, const char *key_file,
              weft_interactor_t in);

// The transport as a service composes it. Borrows `w`.
weft_transport_t wt_transport(wt_t *w);

// Set when an interactor asked the service to wind down through this transport.
int wt_stopped(const wt_t *w);

void wt_close(wt_t *w);

#endif
