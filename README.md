# Junk Drawer

**A Fading Fractal Audio plugin.** An 8-track sample sequencer: cut each sample into 4-6 regions, then let every step choose which piece plays.
Built with JUCE 8 (VST3 and Standalone), same setup as DriftLooper. See [ROADMAP.md](ROADMAP.md) for the plan and the final UI design.

## Where things stand (Stage 0)

The shell is in place; there is no sample playback yet.

* **Four pages**, all in the final layout: Drawer, LFOs, Master, Matrix.
* **Real parameters** (every control on the Master and LFO pages is a host-automatable parameter): master Volume, Pan, Pitch (+ range), Rate (+ mode), Swing, Width; Filter, Delay, Reverb and Limiter with their modes and options; per-track mute, solo, level and pan; settings for all six LFOs.
* **Audio:** stereo pass-through with master Volume and Pan, and a live output meter. Nothing else acts on the sound yet.
* **Not built yet:** loading samples, regions, the sequencer, the LFO engine and assignments (the Matrix page is an empty state), the effects' DSP, recording, presets. The Drawer page shows an empty drawer.

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target JunkDrawer_VST3     # or JunkDrawer_Standalone
```

JUCE 8.0.8 is fetched automatically. On Linux you need the usual JUCE dev packages (ALSA, X11, Xrandr, Xinerama, Xcursor, FreeType, fontconfig, GL).

## Look at every page and check the layout

```
cmake -S . -B build -G Ninja -DJD_BUILD_TESTS=ON
cmake --build build --target JunkUiSnapshot
xvfb-run -a build/JunkUiSnapshot_artefacts/JunkUiSnapshot snapshots     # drop xvfb-run if you have a display
```

This writes `snapshots/drawer.png`, `lfos.png`, `master.png` and `matrix.png`, and fails if any control sits outside the window, two controls overlap, or a button's text does not fit.

## Layout rules

No text below 11 px. No overlapping or crowded text. American English. Every control must be LFO-assignable once the LFO engine lands.

## Source map

| File | What it holds |
| --- | --- |
| `Params.h` | Every parameter ID and range |
| `PluginProcessor.*` | Audio, state, peak meter |
| `PluginEditor.*` | Header and the four pages |
| `Ui.h` | Palette, wood knob, segmented control, panel, shared drawing |
| `Tests/UiSnapshot.cpp` | Page renders and the layout check |
