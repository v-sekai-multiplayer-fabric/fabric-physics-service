// The parameter block.
//
// This node is the reason RFD 0112 chose Lexical. A parameter has to be one thing to the caret:
// the player crosses it in one key press, cannot edit it in place, and removes it whole. A
// `DecoratorNode` is that by construction, because Lexical treats what the decorator draws as
// opaque and never puts a selection inside it.
//
// The decorator is plain DOM. There is no React here, and `decorate` returning an element is the
// vanilla path: `registerDecoratorListener` hands back a map of node key to whatever `decorate`
// produced, and `main.js` appends each element into the node's own DOM.

import { DecoratorNode } from 'lexical';

export class CommandPillNode extends DecoratorNode {
	static getType() {
		return 'command-pill';
	}

	// A clone keeps the key. Lexical clones a node on every write to the editor state, and a
	// clone that dropped the key would be a new node, so the caret would lose its place mid-edit.
	static clone(node) {
		return new CommandPillNode(node.__name, node.__value, node.__key);
	}

	static importJSON(json) {
		return new CommandPillNode(json.name, json.value);
	}

	constructor(name, value, key) {
		super(key);
		this.__name = name;
		this.__value = value;
	}

	exportJSON() {
		return { type: 'command-pill', version: 1, name: this.__name, value: this.__value };
	}

	createDOM() {
		const span = document.createElement('span');
		span.className = 'pill';
		// Lexical marks a decorator's host uneditable itself. Saying so here as well is what makes
		// it true in the DOM the test reads, rather than true only in the editor state.
		span.setAttribute('contenteditable', 'false');
		span.setAttribute('data-pill', this.__name);
		return span;
	}

	updateDOM() {
		// The host never changes. A new value is a new node, because a parameter the player has
		// replaced is a different parameter and the caret should treat it as one.
		return false;
	}

	decorate() {
		const el = document.createElement('span');
		el.className = 'pill-body';
		el.setAttribute('data-pill-value', this.__value);
		// The name is what the player reads and the value is what the server is sent. They differ
		// for a Spark: the menu offers "Spark 12" and the ward is told `12`.
		el.textContent = this.__name === 'verb' ? this.__value : `${this.__name} ${this.__value}`;
		return el;
	}

	// Inline, so it sits in the line of typing rather than breaking it into blocks.
	isInline() {
		return true;
	}

	// What the ward is sent. `getTextContent` is what `root.getTextContent()` walks, so the
	// serialized command falls out of the editor state rather than being assembled beside it —
	// which is what keeps the line the player sees and the line the server parses the same line.
	getTextContent() {
		return this.__value;
	}

	getValue() {
		return this.__value;
	}

	getName() {
		return this.__name;
	}
}

export function $createCommandPillNode(name, value) {
	return new CommandPillNode(name, String(value));
}

export function $isCommandPillNode(node) {
	return node instanceof CommandPillNode;
}
