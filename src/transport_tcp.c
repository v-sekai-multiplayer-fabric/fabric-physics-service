// See transport_tcp.h. Moved out of `queen.c`'s `serve`, unchanged in what it does to a
// socket and changed only in what it answers to: an interactor it is handed, rather than a
// command handler it was compiled beside.
//
// SPDX-License-Identifier: Apache-2.0

#include "transport_tcp.h"

#include "interactor.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 64

typedef struct {
	int fd;
	size_t n;
	char line[WARD_COMMAND_MAX];
} client_t;

struct tcp_t {
	int lfd;
	int nc;
	client_t cl[MAX_CLIENTS];
	weft_interactor_t in;
	unsigned char *reply;
	int stop;
};

static int listen_on(int port) {
	struct sockaddr_in6 a;
	int fd = socket(AF_INET6, SOCK_STREAM, 0), on = 1, off = 0;
	if (fd < 0) return -1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
	// One socket for both families. Fly gives an app an IPv6 address and may give it no IPv4 at
	// all, and a v6-only listener that a v4 client cannot reach is the failure that looks like a
	// firewall. RFD 0112 records the other half of this: Chromium's resolver can fail where curl
	// succeeds on a v6-only app, so a test navigates to the bracketed literal.
	setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);

	memset(&a, 0, sizeof a);
	a.sin6_family = AF_INET6;
	a.sin6_addr = in6addr_any;
	a.sin6_port = htons((uint16_t)port);
	if (bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 16)) {
		close(fd);
		return -1;
	}
	return fd;
}

// Four bytes of length and then the reply, so a reader knows how much to wait for without
// scanning for a delimiter that a venue's name could contain.
static int send_reply(int fd, const unsigned char *body, size_t n) {
	unsigned char head[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16),
	                         (unsigned char)(n >> 8), (unsigned char)n};
	size_t sent = 0;
	if (send(fd, head, 4, MSG_NOSIGNAL) != 4) return 1;
	while (sent < n) {
		const ssize_t w = send(fd, body + sent, n - sent, MSG_NOSIGNAL);
		if (w <= 0) return 1;
		sent += (size_t)w;
	}
	return 0;
}

static void drop(tcp_t *t, int i) {
	close(t->cl[i].fd);
	t->cl[i] = t->cl[t->nc - 1];
	t->nc--;
}

static int fds_of(void *ctx, int *out, int max) {
	tcp_t *t = (tcp_t *)ctx;
	int n = 0;
	if (n < max) out[n++] = t->lfd;
	for (int i = 0; i < t->nc && n < max; i++) out[n++] = t->cl[i].fd;
	return n;
}

static void ready(void *ctx, int fd) {
	tcp_t *t = (tcp_t *)ctx;

	if (fd == t->lfd) {
		const int c = accept(t->lfd, NULL, NULL);
		if (c < 0) return;
		if (t->nc >= MAX_CLIENTS) {
			close(c); // the seats are full, and a silent queue would be worse
			return;
		}
		t->cl[t->nc].fd = c;
		t->cl[t->nc].n = 0;
		t->nc++;
		return;
	}

	int i = 0;
	while (i < t->nc && t->cl[i].fd != fd) i++;
	if (i == t->nc) return;

	char buf[256];
	const ssize_t r = recv(fd, buf, sizeof buf, 0);
	if (r <= 0) {
		drop(t, i);
		return;
	}
	for (ssize_t k = 0; k < r; k++) {
		if (buf[k] == '\r') continue;
		if (buf[k] != '\n') {
			// A line longer than the buffer is dropped rather than truncated. A truncated
			// command is a different command, and running one nobody typed is worse than
			// answering none.
			if (t->cl[i].n + 1 < sizeof t->cl[i].line) t->cl[i].line[t->cl[i].n++] = buf[k];
			continue;
		}
		t->cl[i].line[t->cl[i].n] = '\0';
		t->cl[i].n = 0;

		const size_t n = weft_ask(&t->in, t->cl[i].line, t->reply, WARD_REPLY_MAX, &t->stop);
		if (n == 0 || send_reply(fd, t->reply, n)) {
			drop(t, i);
			return;
		}
	}
}

static void closer(void *ctx) { tcp_close((tcp_t *)ctx); }

tcp_t *tcp_open(int port, weft_interactor_t in) {
	tcp_t *t = calloc(1, sizeof *t);
	if (!t) return NULL;
	t->in = in;
	t->reply = malloc(WARD_REPLY_MAX);
	t->lfd = t->reply ? listen_on(port) : -1;
	if (t->lfd < 0) {
		fprintf(stderr, "the ward has nowhere to listen on port %d\n", port);
		free(t->reply);
		free(t);
		return NULL;
	}
	return t;
}

weft_transport_t tcp_transport(tcp_t *t) {
	weft_transport_t tr = {fds_of, ready, closer, t};
	return tr;
}

int tcp_stopped(const tcp_t *t) { return t->stop; }

void tcp_close(tcp_t *t) {
	if (!t) return;
	for (int i = 0; i < t->nc; i++) close(t->cl[i].fd);
	if (t->lfd >= 0) close(t->lfd);
	free(t->reply);
	free(t);
}
