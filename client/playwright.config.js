// The interface tests, in a real browser.
//
// RFD 0112 chose Lexical for how a parameter behaves under the caret, and that behaviour exists
// only in a browser: a `contenteditable` element's selection model is the browser's, not the
// library's. So these run in Chromium against the built bundle, and there is no jsdom shortcut —
// jsdom has no caret and would agree with any implementation at all.

import { defineConfig, devices } from '@playwright/test';

export default defineConfig({
	testDir: './tests',
	fullyParallel: true,
	forbidOnly: !!process.env.CI,
	reporter: process.env.CI ? 'list' : [['list']],
	use: {
		baseURL: 'http://127.0.0.1:8123',
		trace: 'on-first-retry',
	},
	projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
	// esbuild builds the bundle and serves `public/` in one step, so a test can never run against
	// a stale bundle from a previous edit.
	webServer: {
		command: 'npm run serve -- --serve=127.0.0.1:8123',
		url: 'http://127.0.0.1:8123',
		reuseExistingServer: !process.env.CI,
		timeout: 60000,
	},
});
