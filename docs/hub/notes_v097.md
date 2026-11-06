The firmware is now built and signed on GitHub.

- CHOPPAH's packages are built on GitHub by the shared FM-1 builder and signed with build provenance, so the Firmware Hub shows CHOPPAH as "Built from source on GitHub" and anyone can check that the `.fwsc` they install is the one this commit builds.
- A release is a tag now, not a merge: a push to main builds and tests the firmware, but publishes no release. The tag `vX.Y` makes the release.
- The site is built from the same, signed package as the release, so the download, the web installer and the Hub serve the same bytes.
- The release notes of a version live in `docs/hub/notes_vXY.md` and are set on the release with it.

The firmware itself is unchanged: eight Amen-family breaks, sixteen slices each, a live generator, a four-slot FX chain and a DJ filter — and it installs on the newer units with the UC25HQ80 flash.
