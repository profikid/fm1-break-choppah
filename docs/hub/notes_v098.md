Bluetooth audio: the FM-1 now sends the channel mode it agreed with your speaker.

- The fix: the SBC encoder used the channel mode the app asked for, while the mode that was negotiated with the speaker could be another one. A speaker that was told "stereo" and receives joint-stereo frames reads the wrong channel layout, and one of the two channels falls silent — the "only the right side" complaint (on Polly, LOOM-8 and Optimist alike). The mode that was agreed now decides, always: the encoder follows the negotiation, and an app can no longer overrule it.
- Plain stereo first: when your speaker offers both, the FM-1 asks for plain stereo, the mode every speaker decodes. A few speakers decode joint stereo wrongly, and then a channel falls silent.
- **K2 while streaming is CODEC** now has three positions: **AUTO** (the default: plain stereo when the speaker has it), **STEREO** and **JOINT**. The mode is negotiated with the speaker, so a change takes effect at the **next connection**, not while streaming.

This is a firmware fix in fm1-base (the Bluetooth audio stack). CHOPPAH carries it from the base's `fix/bt-a2dp-stereo-capture` branch; the upstream pull request follows after the change has been heard on an FM-1.

Everything else is unchanged: eight Amen-family breaks, sixteen slices each, a live generator, a four-slot FX chain and a DJ filter — and it installs on the newer units with the UC25HQ80 flash.
