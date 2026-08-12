// A line-framed TCP transport.
//
// One command per line in, one length-prefixed reply out. The prefix is this transport's and
// not the interactor's: a byte stream has no boundary of its own, so somebody has to supply
// one, and that somebody is whoever owns the stream. WebTransport supplies none because a
// stream FIN already is one.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef QUEEN_TRANSPORT_TCP_H
#define QUEEN_TRANSPORT_TCP_H

#include "weft/interactor.h"

typedef struct tcp_t tcp_t;

// Binds every interface, v4 and v6. Returns NULL if the port is not free.
tcp_t *tcp_open(int port, weft_interactor_t in);

// The transport as a service composes it. Borrows `t`.
weft_transport_t tcp_transport(tcp_t *t);

// Set when an interactor asked the service to wind down through this transport.
int tcp_stopped(const tcp_t *t);

void tcp_close(tcp_t *t);

#endif
