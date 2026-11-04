Units sold since autumn 2026 (mostly AliExpress) carry a **UC25HQ80** flash (JEDEC `B3 60 14`, 1 MiB, the same plain SPI NOR commands) instead of the stock **Puya P25Q80** (`85 60 14`).

`device/main_fm1.c` only knew the Puya:

```c
flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
```

On a UC25HQ80 unit `flash_ok` stays false, and with it:

- the store's save returns -9, so **SAVE fails** (the settings and the patterns never reach the flash);
- the data area cannot be written;
- the backup and the restore from the host are ignored;
- the update loader is never started (`ota_session()` is behind `flash_ok`), so **no firmware can be installed from that unit at all**.

The Firmware Hub's submit page names this case ("newer FM-1s have a different flash chip ... it takes these units for unknown: saves stay in RAM ('flash error') and the update loader refuses to install"), and sloopDX and OberVave already accept both parts.

The fix is in pull request #35: one macro, `FM1B_FLASH_JEDEC_OK`, in a new `device/fm1_flash_ok.h`, with a test.

Two other spots accept only the Puya today, both in felucca-bsp (Leo's repo), so I left them alone:

- `bsp/tools/fm1_rescue.py` refuses a device whose flash id is not `856014`, so the rescue script does not run on a UC25HQ80 unit;
- `bsp/examples/hwtest/hwtest.c` has the same one-line check (an example, not the platform).

Worth noting: the loader itself has no flash-id gate, so it writes the package on either part. Only the app needs the change.
