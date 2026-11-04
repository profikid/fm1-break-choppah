Hi Charles — a follow-up on this one, since it affects every firmware that builds from `main`.

I put the test run in the PR description, because fm1-base has no CI to show it: `flash-ok` passes, and the whole suite gives the same result on this branch as on a clean checkout of `main` (the failing ones — `host-build`, `mirror`, `mirror-viewer`, `mirror-png`, `template` — fail the same way without this change).

I also opened an issue for it, so it shows up in your notifications: #36.

Two other spots accept only the Puya today, both in felucca-bsp (Leo's repo), so I left them alone and only mentioned them in the issue:

- `bsp/tools/fm1_rescue.py` refuses a device whose flash id is not `856014`, so the rescue script does not run on a UC25HQ80 unit;
- `bsp/examples/hwtest/hwtest.c` has the same one-line check (an example, not the platform).

The loader itself has no flash-id gate, so it writes the package on either part — only the app needs this change.

If you would rather have it somewhere else (the bsp's `fm1_flash.h`, or a different name for the macro), say so and I will move it. Merged would be appreciated: it is a two-file change plus a test, and without it every firmware that builds from `main` today still takes the newer units for unknown.

CHOPPAH pins `base/` to `e75b072` until it lands, so it does not block on the merge.
