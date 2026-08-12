// WebTransport termination, in the Queen's own process.
//
// Mirrored from `thirdparty/gateway-edge/transport/`, not linked from it: that copy is a
// subtree and carries zone-server's tick, its `z_id` and its iceoryx2 waitset, none of which
// this process has. Mirroring is what `cmake/picoquic.cmake` already does for the same reason
// — a divergence inside the subtree is lost at the next `git subtree pull`, and the build
// repair belongs upstream in `fabric-gateway-edge`.
//
// The transport itself is picoquic's: `picohttp/webtransport.c` and `h3zero_*`. Nothing here
// implements WebTransport. This is the glue that hands a command to the ward and the ward's
// answer back.
#ifndef QUEEN_WT_H
#define QUEEN_WT_H

#include <stddef.h>

typedef struct wt_t wt_t;

// A command arrived whole. There is no length prefix and no delimiter: the client opened a
// bidirectional stream, wrote the line and closed its side, so the FIN is the boundary. The
// handler writes CBOR into `reply` and returns its length; 0 says nothing back.
typedef size_t (*wt_command_fn)(void *app, const char *line, unsigned char *reply, size_t cap,
                                int *stop);

// `bind_addr` is a numeric address or NULL for every interface. Fly needs the literal
// `fly-global-services` resolved here: a UDP socket bound to `::` or `0.0.0.0` there replies
// from the wrong source address and the packets are dropped on the way out.
wt_t *wt_open(int port, const char *bind_addr, const char *cert_file, const char *key_file,
              wt_command_fn on_command, void *app);

// The two descriptors the caller's poll loop must watch: the UDP socket, and picoquic's own
// protocol timer.
void wt_fds(wt_t *w, int *udp_fd, int *timer_fd);

void wt_readable(wt_t *w);
void wt_tick(wt_t *w);
void wt_close(wt_t *w);

#endif
