# CHOPPAH

CHOPPAH turns the M-VAVE FM-1 into an Amen chopper: a breakbeat cut into sixteen slices on the white keys,
momentary effect pads on the black keys (rolls, reverse, freeze, half speed, tape stop, gate), a generator that
rearranges the break as it plays, a master effects chain you stack and reorder (filter, crunch, drive, punch, flanger,
ring mod, tremolo, a plate reverb, ping-pong delay, EQ, tape, a multiband compressor, a clipper), a four-layer gabber
kick with its own steps, a DJ filter on the break and a mixer.

![CHOPPAH playing: the break's waveform with the slice sounding lit, sixteen slice blocks below, the pads and the knobs](manual/img/screen-slicer.png "CHOPPAH's slicer view")

Version {{VERSION}}. Installing is at your own risk; the installer can put M-VAVE's own firmware back. The public
build has 8 classic breaks, 2 bars each: Amen, Funky Drummer, The Worm, Think, Cold Sweat, Hot Pants, Apache, Sport.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): the slicer, the pads, the generator, the effects.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 tools/fm1_install.py {{PKG}}`.

Built on fm1-base and felucca-bsp, from Felucca by Leo Kuroshita, Hügelton Instruments. MPL-2.0.
M-VAVE and FM-1 are trademarks of their owners. CHOPPAH is not affiliated with M-VAVE.
