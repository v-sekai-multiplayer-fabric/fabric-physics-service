// See wt.h. The call sequence is picoquic's own reference server, and the notes that record
// which line came from where are in `thirdparty/gateway-edge/transport/wt_session.c`, which
// this mirrors.

#include "wt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <h3zero_common.h>
#include <pico_webtransport.h>
#include <picoquic.h>
#include <picoquic_utils.h>

#define WT_PATH "/ward"
#define WT_RECV_BUF 2048
#define WT_SEND_BUF 2048
#define WT_LINE_MAX 512
#define WT_REPLY_MAX 262144

// Fly forwards UDP with about 72 bytes of its own overhead — 60 for WireGuard and 12 for the
// forwarding metadata — so the usable payload is nearer 1300 than 1500. picoquic would
// otherwise probe up to a path MTU that silently blackholes.
#define WT_MTU_MAX 1300

// One in-flight command per stream, and a bounded number of them. A ward that cannot answer
// is a ward with one writer, so this is a queue depth and not a concurrency setting.
#define WT_MAX_STREAMS 16

typedef struct {
	uint64_t stream_id;
	int in_use;
	size_t n;                  // command bytes so far
	char line[WT_LINE_MAX + 1];
	unsigned char *reply;      // NULL until the FIN was seen and the ward answered
	size_t reply_n, reply_sent;
} wt_stream_t;

typedef struct {
	wt_command_fn on_command;
	void *app;
	int *stop;
	wt_stream_t s[WT_MAX_STREAMS];
} wt_session_t;

struct wt_t {
	picoquic_quic_t *quic;
	// The path table, and the parameters that name it. Both outlive every connection: h3zero
	// reads them each time it builds a per-connection context, so neither may be a local.
	picohttp_server_path_item_t *table;
	picohttp_server_parameters_t *params;
	wt_session_t *session;
	int udp_fd;
	int timer_fd;
	struct sockaddr_storage local;
	socklen_t local_len;
	int stop;
};

static wt_stream_t *stream_find(wt_session_t *w, uint64_t id, int make) {
	wt_stream_t *free_slot = NULL;
	for (int i = 0; i < WT_MAX_STREAMS; i++) {
		if (w->s[i].in_use && w->s[i].stream_id == id) return &w->s[i];
		if (!w->s[i].in_use && !free_slot) free_slot = &w->s[i];
	}
	if (!make || !free_slot) return NULL;
	memset(free_slot, 0, sizeof *free_slot);
	free_slot->in_use = 1;
	free_slot->stream_id = id;
	return free_slot;
}

static void stream_release(wt_stream_t *s) {
	free(s->reply);
	memset(s, 0, sizeof *s);
}

// ── The session ───────────────────────────────────────────────────────────────

static int wt_session_callback(picoquic_cnx_t *cnx, uint8_t *bytes, size_t length,
                               picohttp_call_back_event_t wt_event,
                               h3zero_stream_ctx_t *stream_ctx, void *path_app_ctx) {
	wt_session_t *w = (wt_session_t *)path_app_ctx;
	if (!w || !stream_ctx) return 0;

	switch (wt_event) {
	case picohttp_callback_post_data:
	case picohttp_callback_post_fin: {
		wt_stream_t *s = stream_find(w, stream_ctx->stream_id, 1);
		if (!s) return 0; // every seat is taken; the stream is answered by silence

		// A command longer than the buffer is dropped rather than truncated, for the reason
		// `serve` gives: a truncated command is a different command.
		for (size_t i = 0; i < length; i++) {
			const char c = (char)bytes[i];
			if (c == '\r' || c == '\n') continue;
			if (s->n < WT_LINE_MAX) s->line[s->n++] = c;
		}

		if (wt_event != picohttp_callback_post_fin) return 0;

		// The FIN is the boundary. There is no framing on the way in and none on the way out.
		s->line[s->n] = '\0';
		unsigned char *reply = malloc(WT_REPLY_MAX);
		if (!reply) {
			stream_release(s);
			return 0;
		}
		s->reply_n = w->on_command(w->app, s->line, reply, WT_REPLY_MAX, w->stop);
		if (s->reply_n == 0) {
			free(reply);
			stream_release(s);
			return 0;
		}
		s->reply = reply;
		s->reply_sent = 0;
		picoquic_mark_active_stream(cnx, stream_ctx->stream_id, 1, stream_ctx);
		return 0;
	}

	case picohttp_callback_provide_data: {
		wt_stream_t *s = stream_find(w, stream_ctx->stream_id, 0);
		if (!s || !s->reply) {
			(void)picoquic_provide_stream_data_buffer(bytes, 0, 0, 0);
			return 0;
		}
		size_t left = s->reply_n - s->reply_sent;
		size_t take = left < length ? left : length;
		const int more = (take < left);
		uint8_t *buf = picoquic_provide_stream_data_buffer(bytes, take, !more, more);
		if (!buf) return -1;
		memcpy(buf, s->reply + s->reply_sent, take);
		s->reply_sent += take;
		// `!more` above already carried the FIN, so the answer ends where the buffer does and
		// the client needs no length to know it.
		if (!more) stream_release(s);
		return 0;
	}

	case picohttp_callback_reset:
	case picohttp_callback_stop_sending: {
		wt_stream_t *s = stream_find(w, stream_ctx->stream_id, 0);
		if (s) stream_release(s);
		return 0;
	}

	default:
		return 0;
	}
}

static int wt_connect_callback(picoquic_cnx_t *cnx, uint8_t *bytes, size_t length,
                               picohttp_call_back_event_t wt_event,
                               h3zero_stream_ctx_t *stream_ctx, void *path_app_ctx) {
	(void)bytes;
	(void)length;
	if (wt_event == picohttp_callback_connect) {
		// A client that sends no WT_AVAILABLE_PROTOCOLS gets -1 here, and that is not fatal:
		// this path has one purpose, so an unnegotiated protocol name is unrecorded rather
		// than refused. The vendored copy flags the same thing.
		(void)picowt_select_wt_protocol(stream_ctx, "ward");
		stream_ctx->path_callback = wt_session_callback;
		stream_ctx->path_callback_ctx = path_app_ctx;

		// Accepting the CONNECT is not the same as joining the session to its streams. Every
		// stream a client opens inside the session carries the control stream's id as its
		// prefix, and h3zero routes it by looking that prefix up. Without this declaration the
		// lookup misses and h3zero resets each stream the moment it arrives — which is what
		// `gateway-edge/transport/wt_session.c` does, because it stops at the two lines above.
		h3zero_callback_ctx_t *h3 = (h3zero_callback_ctx_t *)picoquic_get_callback_context(cnx);
		if (!h3) return -1;
		stream_ctx->ps.stream_state.control_stream_id = stream_ctx->stream_id;
		return h3zero_declare_stream_prefix(h3, stream_ctx->stream_id, wt_session_callback,
		                                    path_app_ctx);
	}
	return 0;
}

// ── The socket ────────────────────────────────────────────────────────────────

static int udp_bind(int port, const char *bind_addr, struct sockaddr_storage *out,
                    socklen_t *out_len) {
	struct addrinfo hints, *res = NULL;
	char portstr[16];
	int fd = -1;

	snprintf(portstr, sizeof portstr, "%d", port);
	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;
	// No family. Fly's UDP is IPv4 only and its private network is IPv6, so which one this is
	// depends entirely on the address the caller was given, and guessing either way is how a
	// socket ends up bound to something the platform will not route.
	hints.ai_family = bind_addr ? AF_UNSPEC : AF_INET6;

	if (getaddrinfo(bind_addr, portstr, &hints, &res) != 0 || !res) return -1;

	fd = socket(res->ai_family, SOCK_DGRAM, 0);
	if (fd < 0) {
		freeaddrinfo(res);
		return -1;
	}
	int on = 1, off = 0;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
	if (res->ai_family == AF_INET6) setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);

	if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
		close(fd);
		freeaddrinfo(res);
		return -1;
	}
	memcpy(out, res->ai_addr, res->ai_addrlen);
	*out_len = res->ai_addrlen;
	freeaddrinfo(res);
	return fd;
}

static void arm_timer(wt_t *w, uint64_t now) {
	uint64_t next = picoquic_get_next_wake_time(w->quic, now);
	int64_t delta = (int64_t)(next - now);
	if (delta < 1000) delta = 1000; // a negative delta would spin the poll loop
	struct itimerspec its;
	memset(&its, 0, sizeof its);
	its.it_value.tv_sec = delta / 1000000;
	its.it_value.tv_nsec = (delta % 1000000) * 1000;
	timerfd_settime(w->timer_fd, 0, &its, NULL);
}

static void flush(wt_t *w) {
	uint8_t out[WT_SEND_BUF];
	size_t n = 0;
	struct sockaddr_storage to, from;
	int if_index = 0;
	picoquic_connection_id_t log_cid;
	picoquic_cnx_t *last = NULL;
	const uint64_t now = picoquic_current_time();

	for (;;) {
		if (picoquic_prepare_next_packet(w->quic, now, out, sizeof out, &n, &to, &from,
		                                 &if_index, &log_cid, &last) != 0 ||
		    n == 0)
			break;
		(void)sendto(w->udp_fd, out, n, 0, (struct sockaddr *)&to,
		             to.ss_family == AF_INET6 ? sizeof(struct sockaddr_in6)
		                                      : sizeof(struct sockaddr_in));
	}
	arm_timer(w, now);
}

void wt_readable(wt_t *w) {
	uint8_t in[WT_RECV_BUF];
	struct sockaddr_storage peer;
	socklen_t peer_len;

	for (;;) {
		peer_len = sizeof peer;
		const ssize_t n =
		    recvfrom(w->udp_fd, in, sizeof in, MSG_DONTWAIT, (struct sockaddr *)&peer, &peer_len);
		if (n <= 0) break;
		// The real bound address, from the socket itself. The vendored copy hardcodes AF_INET6
		// here, which is wrong the moment the socket is the IPv4 one Fly requires.
		picoquic_incoming_packet(w->quic, in, (size_t)n, (struct sockaddr *)&peer,
		                         (struct sockaddr *)&w->local, 0, 0, picoquic_current_time());
	}
	flush(w);
}

void wt_tick(wt_t *w) {
	uint64_t fired;
	// A timerfd stays readable until it is read. The result is genuinely unwanted, and a cast
	// to void does not silence glibc's warn_unused_result.
	const ssize_t got = read(w->timer_fd, &fired, sizeof fired);
	(void)got;
	flush(w);
}

void wt_fds(wt_t *w, int *udp_fd, int *timer_fd) {
	*udp_fd = w->udp_fd;
	*timer_fd = w->timer_fd;
}

// ── Open and close ────────────────────────────────────────────────────────────

wt_t *wt_open(int port, const char *bind_addr, const char *cert_file, const char *key_file,
              wt_command_fn on_command, void *app) {
	wt_t *w = calloc(1, sizeof *w);
	if (!w) return NULL;
	w->udp_fd = w->timer_fd = -1;

	w->session = calloc(1, sizeof *w->session);
	if (!w->session) goto fail;
	w->session->on_command = on_command;
	w->session->app = app;
	w->session->stop = &w->stop;

	w->udp_fd = udp_bind(port, bind_addr, &w->local, &w->local_len);
	if (w->udp_fd < 0) {
		fprintf(stderr, "the ward cannot bind UDP %s:%d: %s\n", bind_addr ? bind_addr : "[::]",
		        port, strerror(errno));
		goto fail;
	}

	w->table = calloc(1, sizeof *w->table);
	w->params = calloc(1, sizeof *w->params);
	if (!w->table || !w->params) goto fail;
	w->table[0].path = WT_PATH;
	w->table[0].path_length = strlen(WT_PATH);
	w->table[0].path_callback = wt_connect_callback;
	w->table[0].path_app_ctx = w->session;
	w->params->path_table = w->table;
	w->params->path_table_nb = 1;

	// picoquic writes its diagnostics to a stream nobody has pushed, so by default they go
	// nowhere and a refused certificate is a silent NULL. This is the switch that makes it
	// speak, and it is worth having on a machine where the alternative is guessing.
	if (getenv("QUEEN_QUIC_DEBUG")) debug_printf_push_stream(stderr);

	uint8_t reset_seed[PICOQUIC_RESET_SECRET_SIZE] = {0};
	// The default callback context is the parameters, and not a context built from them.
	// `h3zero_callback` builds its own per connection, casting whatever it is given here to
	// `picohttp_server_parameters_t *` (h3zero_common.c, at the `callback_ctx == default`
	// branch). Handing it a ready-made `h3zero_callback_ctx_t *` — which is what
	// `gateway-edge/transport/wt_session.c` does — reads the path table from the wrong offset
	// and segfaults on the first packet. That copy has never run, which is how it survived.
	w->quic = picoquic_create(256, cert_file, key_file, NULL, "h3", h3zero_callback, w->params,
	                          NULL, NULL, reset_seed, picoquic_current_time(), NULL, NULL, NULL, 0);
	if (!w->quic) {
		fprintf(stderr, "picoquic refused the certificate at %s\n", cert_file ? cert_file : "(none)");
		goto fail;
	}

	// What makes this a WebTransport server rather than an HTTP/3 one: the datagram and
	// settings transport parameters h3zero needs before a session can be negotiated.
	picowt_set_default_transport_parameters(w->quic);
	picoquic_set_mtu_max(w->quic, WT_MTU_MAX);

	w->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	if (w->timer_fd < 0) goto fail;
	arm_timer(w, picoquic_current_time());

	printf("the ward speaks WebTransport on UDP %s:%d, at %s\n", bind_addr ? bind_addr : "[::]",
	       port, WT_PATH);
	fflush(stdout);
	return w;

fail:
	wt_close(w);
	return NULL;
}

void wt_close(wt_t *w) {
	if (!w) return;
	if (w->session)
		for (int i = 0; i < WT_MAX_STREAMS; i++)
			if (w->session->s[i].in_use) stream_release(&w->session->s[i]);
	// picoquic_free closes every connection, and each one deletes the h3zero context it made
	// for itself. Nothing here owns one.
	if (w->quic) picoquic_free(w->quic);
	if (w->udp_fd >= 0) close(w->udp_fd);
	if (w->timer_fd >= 0) close(w->timer_fd);
	free(w->params);
	free(w->table);
	free(w->session);
	free(w);
}
