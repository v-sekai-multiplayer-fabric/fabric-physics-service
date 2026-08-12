// What the Queen's ward accepts.
//
// This list is the client's half of `serve_command` in `src/queen.c`, and it exists to be
// compared with it. Each entry names the verb the server parses and the parameters it reads in
// order, because the wire form is `verb arg1 arg2` separated by spaces and nothing else.
//
// `needs` is a rebac relation, and it filters the menu only. A filtered menu is a convenience and
// never an authorization: the server checks again on receipt, and it has to, because a client
// chooses what to show and an attacker writes their own client.

export const COMMANDS = [
	{
		verb: 'look',
		says: 'the ward as it stands',
		params: [],
	},
	{
		verb: 'cycle',
		says: 'let the ward run for a day',
		params: [{ name: 'days', value: '1' }],
	},
	{
		verb: 'commission',
		says: 'turn scrip into a place',
		params: [{ name: 'venue', value: '0' }],
	},
	{
		verb: 'pay',
		says: 'move scrip from the treasury to a purse',
		params: [
			{ name: 'spark', value: '0' },
			{ name: 'amount', value: '10' },
		],
	},
	{
		// The one that reaches everybody. `found_ward` drops and recreates every table, so no
		// interest box excludes it and no player gets to keep the ward they were playing.
		verb: 'restart',
		says: 'found the ward again, for everyone at once',
		params: [],
		needs: 'ward.queen',
	},
];

export function menuFor(relations, typed = '') {
	const held = new Set(relations || []);
	const q = typed.toLowerCase();
	return COMMANDS.filter((c) => !c.needs || held.has(c.needs)).filter((c) =>
		c.verb.startsWith(q),
	);
}
