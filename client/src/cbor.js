// Reading what the ward says.
//
// `queen serve` answers in CBOR behind a four-byte length, so this is the other half of
// `say_ward` in `src/queen.c`. It decodes the subset that side writes and refuses the rest,
// rather than growing into a general CBOR library nobody asked for: an unsigned or negative
// integer, a text string, a definite or indefinite array, a definite map, `true`, `false` and
// `null`.
//
// Refusing the rest is the point. A decoder that guessed at a major type it had never been sent
// would turn a server change into a silently wrong ward on screen, which is the failure mode the
// length prefix and the self-describing encoding exist to avoid.

const BREAK = Symbol('break');

class Reader {
	constructor(bytes) {
		this.b = bytes;
		this.i = 0;
	}

	byte() {
		if (this.i >= this.b.length) throw new Error('the reply ends in the middle of a value');
		return this.b[this.i++];
	}

	// The argument is in the low five bits, spilling into 1, 2, 4 or 8 following bytes as it
	// grows. Eight-byte arguments come back as BigInt and are narrowed by the caller, because a
	// place in micrometres is int64 and Number loses it above 2^53.
	arg(low) {
		if (low < 24) return low;
		if (low === 24) return this.byte();
		if (low === 25) return (this.byte() << 8) | this.byte();
		if (low === 26) {
			let v = 0;
			for (let k = 0; k < 4; k++) v = v * 256 + this.byte();
			return v;
		}
		if (low === 27) {
			let v = 0n;
			for (let k = 0; k < 8; k++) v = (v << 8n) | BigInt(this.byte());
			return v;
		}
		throw new Error(`a CBOR argument of ${low} is not one this reads`);
	}

	value() {
		const head = this.byte();
		const major = head >> 5;
		const low = head & 0x1f;

		if (head === 0xff) return BREAK;

		switch (major) {
			case 0: { // an unsigned integer
				const v = this.arg(low);
				return typeof v === 'bigint' ? narrow(v) : v;
			}
			case 1: { // a negative integer, encoded as -1 - n
				const v = this.arg(low);
				return typeof v === 'bigint' ? narrow(-1n - v) : -1 - v;
			}
			case 3: { // a text string
				const n = Number(this.arg(low));
				const s = new TextDecoder().decode(this.b.subarray(this.i, this.i + n));
				this.i += n;
				return s;
			}
			case 4: { // an array, and the ward sends both lengths
				const out = [];
				if (low === 31) {
					for (;;) {
						const v = this.value();
						if (v === BREAK) break;
						out.push(v);
					}
					return out;
				}
				const n = Number(this.arg(low));
				for (let k = 0; k < n; k++) out.push(this.value());
				return out;
			}
			case 5: { // a map
				const n = Number(this.arg(low));
				const out = {};
				for (let k = 0; k < n; k++) {
					const key = this.value();
					out[key] = this.value();
				}
				return out;
			}
			case 7:
				if (low === 20) return false;
				if (low === 21) return true;
				if (low === 22) return null;
				throw new Error(`a simple value of ${low} is not one this reads`);
			default:
				throw new Error(`a CBOR major type of ${major} is not one this reads`);
		}
	}
}

// A coordinate stays a BigInt only when it has to. Everything the ward sends except a place fits
// in a Number, and a purse that arrived as 12n would compare false against 12 everywhere.
function narrow(v) {
	return v >= -9007199254740991n && v <= 9007199254740991n ? Number(v) : v;
}

export function decode(bytes) {
	return new Reader(bytes).value();
}

// One reply, framed. Returns the value and how many bytes it consumed, so a caller reading a
// stream knows where the next reply starts.
export function decodeFramed(bytes) {
	if (bytes.length < 4) return null;
	const n = ((bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3]) >>> 0;
	if (bytes.length < 4 + n) return null;
	return { value: decode(bytes.subarray(4, 4 + n)), used: 4 + n };
}
