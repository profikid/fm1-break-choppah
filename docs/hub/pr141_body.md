Adds **CHOPPAH** to the emulator's own list (`web/firmwares-own.json`), so it can be played in the browser.

CHOPPAH is an Amen break chopper for the M-VAVE FM-1: eight classic breaks built into the flash, cut into 16 slices on the white keys, a key held to loop it with its own window and playhead, and a generator that rearranges the break live. On top of it a four-slot FX chain (filter, bit crusher, drive, transient shaper), reverb, delay and a mix bus.

- Repository: https://github.com/profikid/fm1-break-choppah
- License: MPL-2.0
- Built on: fm1-base

The package is served from the project's own site (`https://profikid.github.io/fm1-break-choppah/firmware/choppah-latest.fwsc`, with CORS), not from the GitHub release URL: a release URL redirects, and the browser cannot follow that.

The site's own download is the one package of the build that made it, so its name carries the version and the older URL is gone at the next release. The site now publishes the same package under `choppah-latest.fwsc` as well, so the link in this entry keeps working from release to release. The `installer` link is stable too and finds the package itself.

The Hub lists 90 of the 96 firmwares in the catalog, and this one is not on the Hub yet, so it goes in the own list.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
