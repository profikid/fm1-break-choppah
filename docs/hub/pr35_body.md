Units sold since autumn 2026 (mostly AliExpress) carry a UC25HQ80 (JEDEC B3 60 14, 1 MiB, the same plain SPI NOR commands) instead of the stock Puya P25Q80 (85 60 14).

`device/main_fm1.c` only knew the Puya:

```c
flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
```

So on those units `flash_ok` stayed false, and with it:

- the store's save returns -9, so **SAVE failed** (settings and patterns never reached the flash);
- the data area could not be written;
- the backup and restore requests from the host were ignored;
- the update loader was never started (`ota_session()` is behind `flash_ok`), so **a firmware could not be installed from those units**.

The Firmware Hub's submit page calls this out by name ("newer FM-1s have a different flash chip ... it takes these units for unknown: saves stay in RAM ('flash error') and the update loader refuses to install"), and sloopDX and OberVave already accept both parts.

## The change

One macro, `FM1B_FLASH_JEDEC_OK`, in a new `device/fm1_flash_ok.h`: the Puya and the UC25HQ80. Any other answer is still RAM only, nothing written.

The macro lives in fm1-base and not in felucca-bsp's `fm1_flash.h`: the chip layer is header-only and shared with every firmware on it, while the list of the parts is a policy of this platform's storage. An app with a part it has checked for itself can define `FM1B_FLASH_JEDEC_OK` before including the header.

Two files change:

- `device/fm1_flash_ok.h` (new) — the list of the parts the storage may use;
- `device/main_fm1.c` — one line, the boot's check now goes through the macro.

`tests/flash_ok_test.c` covers it: the two parts in, everything else out (no part answered, all ones, a 4 MiB Winbond, a GigaDevice, and the near-misses). It is in `tests/run_tests.sh` as `flash-ok`.

## The tests

There is no CI on fm1-base, so here is the run. `cc -std=c99 -Wall -Wextra -Werror -o flash_ok_test tests/flash_ok_test.c && ./flash_ok_test`:

```
  Puya P25Q80              ok
  UC25HQ80                 ok
  no part answered         ok
  all ones                 ok
  Winbond 4 MiB            ok
  GigaDevice               ok
  near-miss Puya           ok
  near-miss UC25           ok
flash jedec ok
```

The whole suite (`sh tests/run_tests.sh`) gives the same result on this branch as on a clean checkout of `main` (2d8ed74) — the failing ones (`host-build`, `mirror`, `mirror-viewer`, `mirror-png`, `template`) fail the same way on `main` without this change, so this change adds no failure:

```
skip update path (no template/build/template.fwsc: tools/build.py template)
FAIL host-build
FAIL mirror
FAIL mirror-viewer
FAIL mirror-png
FAIL template
skip rfm1 (no emcc or node)
```

## A note on the loader

felucca-bsp's loader has no flash-id gate: it writes the package on either part. Only the app needs this change, so there is nothing to do in the bsp.

## If you would rather take it another way

Two other spots accept only the Puya today, both outside this change and both left alone here:

- `bsp/tools/fm1_rescue.py` refuses a device whose flash id is not `856014`, so the rescue script does not run on a UC25HQ80 unit. It is Leo's repo, so I left it to him.
- `bsp/examples/hwtest/hwtest.c` has the same one-line check; it is an example, not the platform.

CHOPPAH pins `base/` to this branch's commit (`e75b072`) until it lands here, so it does not block on the merge. But any other firmware that builds from `main` today will keep missing the newer units until this is in.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
