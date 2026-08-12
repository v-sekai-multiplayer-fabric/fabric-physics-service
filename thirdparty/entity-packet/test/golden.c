/*
 * The emitted C codec, against the Lean spec's own bytes.
 *
 * `packet_diff.gd` already does this for GDScript: take the 64 golden vectors Lean wrote, encode
 * the same fields in the other language, and compare the hundred bytes. This is that check for
 * C, and it is what the emitted header needs before anything is allowed to trust it. Emitting a
 * header from Codec.lean proves where it came from; only the golden vectors prove it agrees.
 *
 *   cc -I build test/golden.c -o build/golden && build/golden build/packet_golden.csv
 *
 * The CSV is the argument rather than a path baked in, because the emitter writes it to `build/`
 * and the repository keeps a committed copy at the root, and a test that could only see one of
 * them would not notice the two drifting apart.
 *
 * SPDX-License-Identifier: MIT
 */

#include "xr_grid_entity_packet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The generator in EntityPacket/Gen.lean, in C. It is duplicated rather than emitted because it
 * is the test's input and not the thing under test: an input emitted by the same run that
 * emitted the encoder could agree with it for the wrong reason. The arithmetic is Lean's,
 * transcribed, and the CSV is the referee if the transcription is wrong. */
static void mk(int n, xr_grid_entity_packet_t *p)
{
	/* Lean: (n * 2654435761 + k * 40503) % 4000000000 - 2000000000, in Int64. */
	long long f[3];
	short g[6];
	unsigned int u[5];

	for (int k = 0; k < 3; k++) {
		unsigned long long v = ((unsigned long long)n * 2654435761ULL + (unsigned long long)k * 40503ULL) % 4000000000ULL;
		f[k] = (long long)v - 2000000000LL;
	}
	for (int k = 0; k < 6; k++)
		g[k] = (short)((long long)(((unsigned long long)(n + k)) % 65535ULL) - 32767LL);
	for (int k = 0; k < 5; k++)
		u[k] = (unsigned int)(((unsigned long long)n * 7ULL + (unsigned long long)k) % 4294967296ULL);

	memset(p, 0, sizeof *p);
	p->gid = u[1];
	p->pos_um_x = f[0];
	p->pos_um_y = f[1];
	p->pos_um_z = f[2];
	p->vel_x = g[0];
	p->vel_y = g[1];
	p->vel_z = g[2];
	p->hlc = u[2];
	p->class_owner = u[3];
	p->sub_index = u[4];
	p->rot_x = g[3];
	p->rot_y = g[4];
	p->rot_z = g[5];
	for (int i = 0; i < XR_PACKET_PAYLOAD_LEN; i++)
		p->payload[i] = (unsigned char)((n + i * 31) % 256);
}

static void hex_of(const unsigned char *b, char *out)
{
	static const char d[] = "0123456789abcdef";

	for (int i = 0; i < XR_PACKET_SIZE; i++) {
		out[i * 2] = d[b[i] >> 4];
		out[i * 2 + 1] = d[b[i] & 15];
	}
	out[XR_PACKET_SIZE * 2] = '\0';
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "build/packet_golden.csv";
	FILE *f = fopen(path, "r");
	char line[1024], got[XR_PACKET_SIZE * 2 + 1];
	int n = 0, bad = 0;

	if (!f) {
		fprintf(stderr, "cannot read %s -- run `lake exe packet_emit` first\n", path);
		return 2;
	}
	if (!fgets(line, sizeof line, f)) { /* the header row */
		fprintf(stderr, "%s is empty\n", path);
		fclose(f);
		return 2;
	}

	while (fgets(line, sizeof line, f)) {
		xr_grid_entity_packet_t p;
		unsigned char wire[XR_PACKET_SIZE];
		char *comma = strchr(line, ',');

		if (!comma) continue;
		*comma = '\0';

		mk(n, &p);
		xr_grid_entity_packet_encode(&p, wire);
		hex_of(wire, got);

		if (strcmp(got, line) != 0) {
			/* Say which byte, not only that they differ: a hundred hex pairs on two lines is
			 * a diff nobody reads, and the offset names the field. */
			int i = 0;
			while (line[i] && line[i] == got[i]) i++;
			fprintf(stderr, "vector %d differs at byte %d\n  lean %s\n  c    %s\n", n, i / 2,
			        line, got);
			bad++;
		}
		n++;
	}
	fclose(f);

	/* A run that read no vectors passes every comparison it made. */
	if (n == 0) {
		fprintf(stderr, "%s held no vectors\n", path);
		return 2;
	}
	if (bad) {
		fprintf(stderr, "PACKET DIFFERENTIAL FAIL: %d of %d vectors differ\n", bad, n);
		return 1;
	}
	printf("PACKET DIFFERENTIAL PASS: %d of %d vectors match the Lean bytes\n", n, n);
	return 0;
}
