Adds **CHOPPAH** to the emulator's own list (`web/firmwares-own.json`), so it can be played in the browser.

CHOPPAH is an Amen break chopper for the M-VAVE FM-1: eight classic breaks built into the flash, cut into 16 slices on the white keys, a key held to loop it with its own window and playhead, and a generator that rearranges the break live. On top of it a four-slot FX chain (filter, bit crusher, drive, transient shaper), reverb, delay and a mix bus.

- Repository: https://github.com/profikid/fm1-break-choppah
- License: MPL-2.0
- Built on: fm1-base

The package is served from the project's own site (`https://profikid.github.io/fm1-break-choppah/firmware/choppah-0.85.fwsc`, with CORS), not from the GitHub release URL: a release URL redirects, and the browser cannot follow that. The site keeps only the newest package, so the URL follows the release; the `installer` link is the stable one and finds the package itself.

The Hub lists 90 of the 96 firmwares in the catalog, and this one is not on the Hub yet, so it goes in the own list.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
