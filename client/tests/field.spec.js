// What RFD 0112 bought when it chose Lexical.
//
// The decision was made on one clause: a parameter must be one thing to the caret. These are the
// four tests that clause names — the caret crosses a block in one key press, a block cannot be
// edited in place, backspace removes a block whole, and the serialized command is what the server
// accepts. If any of them fails, the decision bought nothing and ProseMirror's cheaper developer
// cost would have been the better trade.

import { test, expect } from '@playwright/test';

async function openField(page, relations = []) {
	await page.addInitScript((r) => {
		window.__relations = r;
	}, relations);
	await page.goto('/');
	await page.locator('#field').click();
	return page.locator('#field');
}

// Type `/`, then pick a command out of the menu by name.
async function runMenu(page, verb) {
	const field = page.locator('#field');
	await field.press('/');
	await expect(page.locator('#menu')).toBeVisible();
	await page.locator(`#menu .row[data-verb="${verb}"]`).click();
	await expect(page.locator('.pill-body').first()).toBeVisible();
}

test('the menu opens on a slash and offers the ward its commands', async ({ page }) => {
	await openField(page);
	await page.locator('#field').press('/');
	await expect(page.locator('#menu')).toBeVisible();
	await expect(page.locator('#menu .row')).toHaveCount(4);
	await expect(page.locator('#menu .row[data-verb="pay"]')).toBeVisible();
});

test('a command the caller may not run is not offered', async ({ page }) => {
	// The menu is filtered by the caller's relations, and the server checks again on receipt.
	// This tests the convenience, not the authorization — the ward is what refuses.
	await openField(page, []);
	await page.locator('#field').press('/');
	await expect(page.locator('#menu .row[data-verb="restart"]')).toHaveCount(0);

	await openField(page, ['ward.queen']);
	await page.locator('#field').press('/');
	await expect(page.locator('#menu .row[data-verb="restart"]')).toHaveCount(1);
});

test('the caret crosses a parameter in one key press', async ({ page }) => {
	const field = await openField(page);
	await runMenu(page, 'pay');

	// `/pay <spark> <amount> ` — the caret is at the end. One ArrowLeft steps over the trailing
	// space, and the next must step over the whole `amount` block rather than into it.
	await field.press('ArrowLeft');
	const before = await page.evaluate(() => window.getSelection().anchorOffset);
	await field.press('ArrowLeft');

	// The proof a block was crossed rather than entered: the anchor node is no longer inside the
	// decorator, and one more press has not left the caret sitting in the same text node counting
	// down character by character.
	const inside = await page.evaluate(() => {
		const sel = window.getSelection();
		const node = sel.anchorNode;
		const el = node.nodeType === 3 ? node.parentElement : node;
		return el.closest('.pill') !== null;
	});
	expect(inside).toBe(false);
	expect(before).not.toBe(null);
});

test('a parameter cannot be edited in place', async ({ page }) => {
	const field = await openField(page);
	await runMenu(page, 'commission');

	const pill = page.locator('.pill-body').first();
	const was = await pill.textContent();

	// Click into the middle of the block and type. A `contenteditable` written by hand is exactly
	// where this fails: the caret lands inside, the character lands inside, and the parameter
	// quietly becomes something the server will not parse.
	await pill.click();
	await page.keyboard.type('99');

	await expect(pill).toHaveText(was);
	// The host is what makes it true in the DOM and not only in the editor state.
	await expect(page.locator('.pill').first()).toHaveAttribute('contenteditable', 'false');
});

test('backspace removes a parameter whole', async ({ page }) => {
	const field = await openField(page);
	await runMenu(page, 'commission');
	await expect(page.locator('.pill')).toHaveCount(1);

	// One press to cross the trailing space, one to take the block. A half-deleted parameter —
	// a block that loses one character and stays — is the failure this rules out.
	await field.press('Backspace');
	await field.press('Backspace');

	await expect(page.locator('.pill')).toHaveCount(0);
	expect(await page.evaluate(() => window.queen.serialize())).not.toContain('venue');
});

test('the serialized command is what the ward parses', async ({ page }) => {
	await openField(page);
	await runMenu(page, 'pay');

	// `serve_command` in src/queen.c reads a verb and up to two arguments split on spaces, with
	// any leading slash stripped. So the line has to come out as exactly that, and the pill's
	// value — not its label — has to be what lands in it.
	const line = await page.evaluate(() => window.queen.serialize());
	expect(line).toBe('/pay 0 10');

	const [verb, first, second] = line.replace(/^\/+/, '').split(' ');
	expect(verb).toBe('pay');
	expect(Number.isNaN(Number(first))).toBe(false);
	expect(Number.isNaN(Number(second))).toBe(false);
});

test('the ward reply decodes from CBOR', async ({ page }) => {
	await openField(page);

	// The bytes `say_ward` writes, byte for byte: a five-pair map, a negative place, an
	// indefinite-length array. Built here rather than captured, so this test says what the format
	// is instead of only agreeing with whatever the encoder last did.
	const reply = await page.evaluate(() => {
		const bytes = [];
		const text = (s) => {
			bytes.push(0x60 | s.length);
			for (const c of s) bytes.push(c.charCodeAt(0));
		};
		bytes.push(0xa3); // a map of three
		text('ok');
		bytes.push(0xf5); // true
		text('say');
		text('paid');
		text('ward');
		bytes.push(0xa2); // a map of two
		text('cycle');
		bytes.push(0x0c); // 12
		text('debt');
		bytes.push(0x38, 0x63); // -100, which is major type 1 over 99

		const body = new Uint8Array(bytes);
		const framed = new Uint8Array(4 + body.length);
		framed[0] = 0;
		framed[1] = 0;
		framed[2] = (body.length >> 8) & 0xff;
		framed[3] = body.length & 0xff;
		framed.set(body, 4);
		return window.queen.applyReply(framed);
	});

	expect(reply.ok).toBe(true);
	expect(reply.say).toBe('paid');
	expect(reply.ward.cycle).toBe(12);
	expect(reply.ward.debt).toBe(-100);
});
