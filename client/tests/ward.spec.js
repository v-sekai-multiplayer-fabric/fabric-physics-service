// The ward, over WebTransport, in a real browser.
//
// `field.spec.js` proves the caret. This proves the wire: Chromium opens a WebTransport
// session against `queen serve`, writes a command on a bidirectional stream and closes its
// side, and the reply comes back as CBOR ended by the server's FIN. There is no length prefix
// in either direction — the stream boundary is the message boundary.
//
// A browser is the client this exists for, so a command line client would prove the transport
// and not the product. It needs a running ward:
//
//   WARD_CERT_HASH=$(openssl x509 -in ward.crt -outform DER | openssl dgst -sha256 -binary \
//                    | openssl base64 -A) npx playwright test ward.spec.js

import { test, expect } from '@playwright/test';
import { decode } from '../src/cbor.js';

const HASH = process.env.WARD_CERT_HASH;
const URL = process.env.WARD_WT_URL || 'https://127.0.0.1:4401/ward';

test.skip(!HASH, 'no WARD_CERT_HASH, so there is no ward to reach');

// Chromium will pin a self-signed certificate by SHA-256 only if it is ECDSA P-256 and valid
// for no more than fourteen days, which is what `generate-tls-cert.sh` makes and why.
async function ask(page, command) {
	const out = await page.evaluate(
		async ([url, hashB64, line]) => {
			const raw = atob(hashB64);
			const hash = new Uint8Array(raw.length);
			for (let i = 0; i < raw.length; i++) hash[i] = raw.charCodeAt(i);

			const wt = new WebTransport(url, {
				serverCertificateHashes: [{ algorithm: 'sha-256', value: hash }],
			});
			await wt.ready;

			const stream = await wt.createBidirectionalStream();
			const writer = stream.writable.getWriter();
			await writer.write(new TextEncoder().encode(line));
			await writer.close(); // the FIN is the boundary, and the only one

			const reader = stream.readable.getReader();
			const chunks = [];
			let total = 0;
			for (;;) {
				const { value, done } = await reader.read();
				if (done) break; // the server's FIN, and the end of the answer
				chunks.push(value);
				total += value.length;
			}
			const all = new Uint8Array(total);
			let at = 0;
			for (const c of chunks) {
				all.set(c, at);
				at += c.length;
			}
			wt.close();
			return Array.from(all);
		},
		[URL, HASH, command]
	);
	// Decoded by the client's own decoder, in Node. The browser carried the bytes; if this
	// refuses them, the page would have refused them too.
	return decode(new Uint8Array(out));
}

test('the ward answers a browser, and is honest to it', async ({ page }) => {
	await page.goto('/');

	const look = await ask(page, '/look');
	expect(look.ok).toBe(true);
	expect(look.say).toBe('the ward as it stands');
	expect(look.honest).toBe(true);

	// The whole reply arrived, not the five pairs an undercounted map would have stopped at.
	expect(Object.keys(look)).toEqual(
		expect.arrayContaining(['ok', 'say', 'honest', 'ward', 'venues', 'board', 'sparks'])
	);
	expect(look.sparks).toHaveLength(look.ward.sparks);

	const purses = look.sparks.reduce((n, s) => n + s.purse, 0);
	expect(look.ward.treasury + purses + look.ward.retired + look.ward.spent).toBe(
		look.ward.issued
	);

	// A command that moves the ward, over the same transport, against the same database the
	// TCP line server writes to.
	const moved = await ask(page, '/cycle 4');
	expect(moved.ok).toBe(true);
	expect(moved.say).toBe('the ward moved');
	expect(moved.ward.cycle).toBe(look.ward.cycle + 4);

	const after = moved.sparks.reduce((n, s) => n + s.purse, 0);
	expect(moved.ward.treasury + after + moved.ward.retired + moved.ward.spent).toBe(
		moved.ward.issued
	);
});
