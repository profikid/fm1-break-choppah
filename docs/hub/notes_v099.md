Bluetooth audio: the codec knob and the left / right test are gone again, and the firmware is on fm1-base's newest main.

- **What goes:** on the BT AUDIO page, **K2 while streaming is no longer CODEC** (AUTO / STEREO / JOINT) and **K3 while streaming is no longer TEST L/R** (the low beep left, the high one right). The page keeps what it is for: K1 Bluetooth audio on / off, K2 the device (the remembered ones first, then what a scan found), K3 a 10-second scan, K4 connect / hang up.
- **Why:** v0.98 changed the SBC encoder so it would follow the channel mode negotiated with the speaker, and gave the app that CODEC knob. On an FM-1 it made no difference at all: the speaker still played only the right channel. The encoder was not the cause, so the fix and the knob it needed both go.
- **The base is upstream again:** CHOPPAH runs `charlesvestal/fm1-base` at `317cb86` — the newest main, with the USB audio channels, the `firmware.json` the firmware hubs read, and the web installer that runs again. The app does not touch the base's SBC encoder any more; the base's own set-up decides, as it always did.
- **The install screen follows the new felucca-bsp:** when the computer goes quiet for two seconds mid-install, the update screen says "USB LOST" and "RECONNECT THE CABLE" instead of sitting there; at the end it says "DONE" and "RESTARTING". Same colours as always, with CHOPPAH's own mark and bar.
- **For the firmware hubs:** the firmware now describes itself in a `firmware.json` (the author, what it is, its tags, the install page, the site and a picture of the screen), so The Firmware Hub, RemoteFM1 and M-Flasher can list it without asking.

Bluetooth audio itself is unchanged: the FM-1 still streams its whole mix to your speaker, in stereo, 150–300 ms late.

Everything else is unchanged too: eight Amen-family breaks, sixteen slices each, a live generator, a four-slot FX chain and a DJ filter — and it installs on the newer units with the UC25HQ80 flash.
