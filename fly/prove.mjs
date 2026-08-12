// fly proxy 4400:4400 --app fabric-store-queen &
// node fly/prove.mjs
//
// Imports the client's decoder rather than carrying a second one: if the browser refuses a
// reply, this refuses it too.
import net from 'node:net';
import { decodeFramed } from '../client/src/cbor.js';

const HOST = process.env.WARD_HOST || '127.0.0.1';
const PORT = Number(process.env.WARD_PORT || 4400);

const script = [
	['/look', 'the ward as it stands'],
	['/cycle 8', 'the ward moved'],
	['/look', null],
	['/cycle 32', 'the ward moved'],
	// The Queen has been building for forty cycles, so which venue is still standing empty is
	// hers to decide and not this file's. A hardcoded id tests whichever answer she gave.
	[
		(r) => {
			const v = r.venues.filter((v) => !v.built && v.cost <= r.ward.treasury);
			return v.length ? `/commission ${v[0].id}` : '/look';
		},
		null,
	],
	['/cycle 8', 'the ward moved'],
	['/look', null],
];

const sock = net.createConnection({ host: HOST, port: PORT });
sock.setNoDelay(true);

let buf = Buffer.alloc(0);
let at = 0;
let failed = 0;
let lastCycle = -1;
let lastIssued = null;

function say(ok, line) {
	if (!ok) failed++;
	console.log(`${ok ? '  ok  ' : ' FAIL '} ${line}`);
}

// A step may be a string or a function of the reply before it, so the prover can commission
// whatever venue the Queen actually left standing empty.
let sent = '';

function send(reply) {
	const step = script[at][0];
	sent = typeof step === 'function' ? step(reply) : step;
	sock.write(sent + '\n');
}

sock.on('connect', () => {
	console.log(`the ward answers on ${HOST}:${PORT}`);
	send(null);
});

sock.on('data', (chunk) => {
	buf = Buffer.concat([buf, chunk]);
	for (;;) {
		const framed = decodeFramed(new Uint8Array(buf));
		if (!framed) return;
		buf = buf.subarray(framed.used);
		check(framed.value);
	}
});

function check(reply) {
	const expect = script[at][1];
	const w = reply.ward;
	console.log(
		`\n${sent}  →  cycle ${w.cycle} · treasury ${w.treasury} · debt ${w.debt} ·` +
			` ${w.sparks} Sparks — "${reply.say}"`
	);

	say(reply.ok === true, 'the ward answered ok');
	if (expect !== null) say(reply.say === expect, `it said "${expect}"`);

	const asked = sent.startsWith('/cycle') ? Number(sent.split(' ')[1]) : 0;
	if (lastCycle >= 0) say(w.cycle - lastCycle === asked, `it moved exactly ${asked}`);
	lastCycle = w.cycle;

	// The same sum `honest()` runs in-process, run again from the far end of a socket. It is
	// the one check that says the deployment did not lose anything on the way out.
	const purses = reply.sparks.reduce((n, s) => n + s.purse, 0);
	const held = w.treasury + purses + w.retired + w.spent;
	say(
		held === w.issued,
		`treasury ${w.treasury} + purses ${purses} + retired ${w.retired} +` +
			` spent ${w.spent} = ${held}, issued ${w.issued}`
	);

	// Scrip is made only by salvage, so `issued` never falls.
	if (lastIssued !== null) say(w.issued >= lastIssued, `issued ${lastIssued} → ${w.issued}`);
	lastIssued = w.issued;

	at++;
	if (at < script.length) send(reply);
	else sock.end();
}

sock.on('error', (e) => {
	console.error(`the ward is not reachable: ${e.message}`);
	process.exit(1);
});

sock.on('close', () => {
	console.log(failed === 0 ? '\nthe ward is honest from out here' : `\n${failed} checks failed`);
	process.exit(failed === 0 ? 0 : 1);
});
