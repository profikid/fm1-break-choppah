# Flashing CHOPPAH (and getting back to stock)

The FM-1 has a **single flash bank and no debug header**. Installing is reliable through fm1-base's update path,
but an interrupted write has no proven recovery, so follow this page step by step.

## Before the first install

1. **Charge the FM-1** and use a data-capable USB-C cable plugged straight into the Mac (no hub).
2. **Keep a stock firmware file.** Download M-VAVE's current FM-1 firmware (`FM-1.fwsc`) from M-VAVE's site or the
   [firmware list](https://fm1-editor.com/firmware/) and keep it in `firmware/stock/` (git-ignored).
3. **Check what's running:**

   ```
   ./build.sh info
   ```

   - `FM-1_0xx`: M-VAVE's stock firmware. Fine to install over.
   - `FM1_OFFICIAL_OPEN` / `FM1_COMMUNITY_nn` (M-VAVE's FM1 Open, V10+): its updater only accepts M-VAVE's own
     packages. **Put stock firmware back first**, then install CHOPPAH.
   - Another community firmware built on Felucca / fm1-base: fine to install over.

## Install

```
./build.sh            # app/build/choppah.fwsc
./build.sh flash      # = python3 base/tools/fm1_install.py app/build/choppah.fwsc
```

The installer finds the FM-1 over USB-MIDI, starts the update loader, writes the package, waits for the restart
and checks the identity (`FM-1_8600` for dev builds). Don't unplug until it says `done`.

If the running firmware can't be reached, put the FM-1 in **update mode**: hold **OCT− + OCT+ for 5 seconds**,
then run `./build.sh flash` again.

A web installer is also possible (Chrome/Edge, Web MIDI): fm1-base's `tools/make_site.py` builds one for a
release (milestone M8).

## Back to stock

```
python3 base/tools/fm1_install.py firmware/stock/FM-1.fwsc
```

## If something goes wrong

| Symptom | What to do |
| --- | --- |
| CHOPPAH crashes at boot twice | fm1-base starts **safe mode**; the installer still reaches it: flash stock or a fixed build |
| A crash screen | Press a key to restart; `python3 base/tools/fm1_remote.py log` reads the crash record for a bug report |
| The install stopped half-way | Run the same install again: it finishes the write if the loader is still in update mode |
| Nothing on screen, but USB shows `4C4A:8057` | The chip's own update mode: `base/bsp/tools/fm1_rescue.sh` puts stock firmware back from a Mac |
| Nothing at all | See the [FM-1-RE recovery notes](https://github.com/AL-255/FM-1-RE) and the awesome-fm-1 recovery section |

## From this project's cloud sessions

The sandboxed shell on the Mac can't see USB devices. Two routes work:

- **macOS Terminal:** `./build.sh flash` from this folder.
- **Chrome (Web MIDI), driven by Claude in Chrome:** on fm1-base's published installer page
  (`charlesvestal.github.io/fm1-base/install/`, where MIDI SysEx is allowed), the package goes into a file input and
  a small module reuses the page's own `Updater` (fm1ota.js) to run `install()`. First done 30 Oct 2026: X0X was
  replaced by CHOPPAH dev `FM-1_8600` (package sha256 `ad17360c…`), about 75 s end to end.
  macOS keeps showing the old MIDI port name ("X0X FM-1") from its cache; the identity handshake is what counts.

### From M-VAVE's FM1 Open back to CHOPPAH

FM1 Open (`FM-1_80000xx`) only accepts M-VAVE's own packages. Put stock back with M-VAVE's workbench
(`open.m-vave.com/fm1-workbench` → Device update → Recovery & force update → **Restore original V15**, which
installs `FM-1_015` and its 128 factory voices), then install CHOPPAH over stock as above. Done this way on
30 Oct 2026. The `FM-1.fwsc` on M-VAVE's file server (21 Oct 2026, md5-verified) fails fm1-base's `checkUfw` on
three small parts (types 52, 251, 161), so fm1-base's own stock restore refuses it; the workbench route works.

