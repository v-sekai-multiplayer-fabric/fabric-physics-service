// The slash command interface.
//
// A field the player types into, a menu that opens on `/`, and parameters that behave as one
// thing to the caret. RFD 0112 chose Lexical for that last clause and nothing else: an `<input>`
// holds plain text, and a `contenteditable` written by hand fails on the caret, on mobile
// autocorrect, on IME composition and on paste — in the input path of every command.
//
// There is no framework under this. Lexical's core is a state model with a DOM reconciler, and
// the decorator listener is the vanilla way to draw a node, so the whole client is these files
// and the bundle esbuild makes of them.

import {
	createEditor,
	$getRoot,
	$getSelection,
	$isRangeSelection,
	$createTextNode,
	$createParagraphNode,
} from 'lexical';
import { registerPlainText } from '@lexical/plain-text';
import { CommandPillNode, $createCommandPillNode } from './pill.js';
import { menuFor } from './commands.js';
import { decodeFramed } from './cbor.js';

const editorEl = document.getElementById('field');
const menuEl = document.getElementById('menu');

// Who the caller is, as far as the menu is concerned. The page is handed its relations by
// whatever served it; the server checks them again on receipt, so this is only what is shown.
const relations = window.__relations || [];

const editor = createEditor({
	namespace: 'queen',
	nodes: [CommandPillNode],
	onError(e) {
		throw e;
	},
});

editor.setRootElement(editorEl);
registerPlainText(editor);

// Draw the decorators. Lexical hands back a map of node key to whatever `decorate` produced, and
// the element goes inside the host the reconciler already made for that node.
editor.registerDecoratorListener((decorators) => {
	for (const [key, el] of Object.entries(decorators)) {
		const host = editor.getElementByKey(key);
		if (!host || !el || host.firstChild === el) continue;
		host.replaceChildren(el);
	}
});

// ── The command as the server reads it ────────────────────────────────────────
//
// `getTextContent` on the root walks the text nodes and asks each pill for its value, so the
// line the player sees and the line the ward parses come from one editor state. Assembling the
// command separately is how the two drift, and the drift shows up as a command that reads right
// on screen and means something else on arrival.
export function serialize() {
	let out = '';
	editor.getEditorState().read(() => {
		out = $getRoot().getTextContent();
	});
	return out.replace(/\s+/g, ' ').trim();
}

// ── The menu ──────────────────────────────────────────────────────────────────

let menu = { open: false, items: [], at: 0, typed: '' };

function drawMenu() {
	menuEl.replaceChildren();
	menuEl.hidden = !menu.open;
	if (!menu.open) return;
	menu.items.forEach((c, i) => {
		const row = document.createElement('div');
		row.className = i === menu.at ? 'row at' : 'row';
		row.setAttribute('data-verb', c.verb);
		row.textContent = `/${c.verb} — ${c.says}`;
		row.addEventListener('mousedown', (e) => {
			e.preventDefault();
			pick(c);
		});
		menuEl.appendChild(row);
	});
}

function openMenu(typed = '') {
	menu = { open: true, items: menuFor(relations, typed), at: 0, typed };
	drawMenu();
}

function closeMenu() {
	menu.open = false;
	drawMenu();
}

// Put the command in the field: the verb as text, and every parameter as a block. The blocks are
// what the player tabs between and what they can never half-delete.
function pick(command) {
	editor.update(() => {
		const root = $getRoot();
		root.clear();
		// `root.clear()` leaves the root empty, and text has to live in a paragraph. The plain-text
		// plugin would make one on the next edit, which is one edit too late for the nodes below.
		const para = $createParagraphNode();
		root.append(para);

		para.append($createTextNode(`/${command.verb}`));
		for (const p of command.params) {
			para.append($createTextNode(' '));
			para.append($createCommandPillNode(p.name, p.value));
		}
		para.append($createTextNode(' '));
		para.selectEnd();
	});
	closeMenu();
	editorEl.focus();
}

editorEl.addEventListener('keydown', (e) => {
	if (menu.open) {
		if (e.key === 'ArrowDown') {
			e.preventDefault();
			menu.at = (menu.at + 1) % Math.max(menu.items.length, 1);
			return drawMenu();
		}
		if (e.key === 'ArrowUp') {
			e.preventDefault();
			menu.at = (menu.at - 1 + menu.items.length) % Math.max(menu.items.length, 1);
			return drawMenu();
		}
		if (e.key === 'Enter') {
			e.preventDefault();
			const c = menu.items[menu.at];
			if (c) pick(c);
			return;
		}
		if (e.key === 'Escape') {
			e.preventDefault();
			return closeMenu();
		}
	}
	// The menu opens on a `/` that starts a command, which is a `/` at the start of the line. A
	// slash inside a word is a slash, not a menu.
	if (e.key === '/') {
		let empty = false;
		editor.getEditorState().read(() => {
			const sel = $getSelection();
			empty = $isRangeSelection(sel) && $getRoot().getTextContent().trim() === '';
		});
		if (empty) setTimeout(() => openMenu(''), 0);
	}
});

// Typing after the slash filters the menu. Reading the state rather than the key event is what
// keeps this right through an IME composition, where the character that lands is not the key
// that was pressed.
editor.registerUpdateListener(() => {
	if (!menu.open) return;
	const text = serialize();
	if (!text.startsWith('/')) return closeMenu();
	openMenu(text.slice(1).split(' ')[0]);
});

// ── The ward ──────────────────────────────────────────────────────────────────
//
// A browser cannot open a TCP socket, and `queen serve` listens on one. Until `queen` has a
// transport a browser can reach, this is where the reply would be applied, and the decoder below
// it is the same one either way — so the client is written against the wire format rather than
// against a mock of it.
export function applyReply(bytes) {
	const framed = decodeFramed(bytes);
	if (!framed) return null;
	const reply = framed.value;
	const board = document.getElementById('ward');
	if (board) {
		board.textContent =
			`cycle ${reply.ward.cycle} · treasury ${reply.ward.treasury} · debt ${reply.ward.debt}` +
			` · ${reply.ward.sparks} Sparks — ${reply.say}`;
	}
	return reply;
}

// What the tests drive, and what a transport would call.
window.queen = { serialize, applyReply, openMenu, closeMenu, editor };
