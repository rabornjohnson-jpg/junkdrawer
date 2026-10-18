# Junk Drawer: Roadmap

A Fading Fractal Audio plugin. An 8-track sample sequencer: cut each sample into 4-6 regions, then let every step choose which piece plays.

**Pitch:** Chop any sound into regions and let the sequencer decide which piece plays on every step.

Status: concept and UI design locked (Oct 2026); build starting. Renamed Oct 2026: the plug-in is now **Rotary Dial** (seafoam tube-radio UI; presets are "stations"; Junk Drawer was the working name and the code and build targets keep it). Run a trademark check before release (see Open Items).

---

## Identity

- **Family feel:** melancholy, warm, but fun. Same Fading Fractal Audio logo and name in the UI header.
- **Concept:** one real junk drawer seen from above. Warm wood, tarnished brass, dusty amber light, real photographed objects. Realistic, not cartoonish.
- **Distinct from DriftLooper:** DriftLooper is about time and wear (record-first, Deck Wear is the hero). Junk Drawer is about selection and rearrangement (load-first, the open drawer with its regions is the hero).
- **Tape wear stays out.** DriftLooper owns that identity.
- **Playback happens only on steps.** No MIDI note triggering.

## Standing requirements

- Every control is LFO-assignable, including switches, menus, and region start/end.
- CPU and RAM efficiency is a priority.
- No overlapping or crowded text or graphics anywhere in the UI.
- American English spelling in all text.
- No occult or supernatural references (including preset names).
- Wants to see and approve UI screenshots before a rebuild.
- Region values are the base; per-step values stack on top as offsets.
- Plain, clear labels on controls. The whimsy lives in tooltips, empty states, and preset names.

## Shared code with DriftLooper (invisible to users)

Step-lane engine, LFO engine, file loading, DAW recording pipeline. Pull these into a common module so a fix in one plugin helps all of them.

---

## Staged build

### Stage 1: Foundation
- Plugin shell with four pages (Drawer, LFOs, Master, Matrix) and the shared header (FFA logo, preset bar, BPM, Host sync, Reduce motion, Settings).
- Shared engine module from DriftLooper.
- Load audio per track by drag and drop or a Browse button (WAV, AIFF, FLAC, MP3).
- Waveform display; resample on load to the session rate.
- One track plays end to end.

### Stage 2: Regions
- 4-6 draggable regions per track, each with a color, name, and start/end markers.
- Snap to transient or zero crossing (toggles).
- Audition, solo, and mute a region.
- Per-region controls: pitch (semitones plus cents), gain, pan, reverse, fade in/out, loop mode (one-shot, loop, ping-pong), filter (cutoff, resonance), envelope (ADSR).
- Auto-slice into N equal parts or by transients.

### Stage 3: Sequencer
- Step grid with a region lane (which region plays on each step).
- Lanes: volume, pitch, ratchet, direction, length. One lane edited at a time via tabs, with a small overview strip.
- Global defaults for step length and playback rate; per-step values are offsets from the default.
- Swing and per-track lane length.

### Stage 4: Recording (built: Record, input, start, length, 60 s cap; also built: Catch Last rolling buffer, Replace/Add choice, input-too-loud warning, takes always saved as WAV files; not done: latency report, true feedback detection)
- Per-track Record button, capturing from the plugin's audio input.
- Sync options: immediate, next bar, or fixed length (1, 2, 4, 8 bars). Short pre-roll buffer.
- Replace or Add (overdub): BUILT. Add mixes the take on top inside the sound's current length; regions stay put.
- Always-on rolling buffer ("Catch Last") that can be dragged into any track.
- Waveform draws live while recording. Every take is saved as a 24-bit WAV in the Recordings folder (BUILT).
- Per-track maximum length to cap memory use. Latency report: not possible from inside a plug-in (the host does not tell it the interface latency); the take starts when the audio arrives.
- "Input too loud" warning when the input clips: BUILT. A real feedback check (the plug-in hearing its own output) is not built; the plug-in cannot see how the host routes things.

### Stage 5: LFOs
- Six LFOs: sine, triangle, saw, square, sample and hold, smooth random, plus a slow "drift" shape.
- Rate (free or tempo-synced, including dotted and triplet), depth, phase, polarity, restart mode.
- Each LFO can be global or per-track. LFO-to-LFO modulation allowed.
- Many-to-many assignments. Pick an LFO (L1-L6) on the LFOs page, click any control, drag up or down to set the amount. Amount is always per target. Assigned controls show a small badge.
- LFO scope: **Global** = one shared instance for the whole plugin, same phase everywhere; **Per track** = every track runs its own copy with shared settings but independent phase. Restart: global follows the transport/bar, per-track follows that track's loop. Master and FX targets are always global.
- Matrix page listing every assignment (target, LFO, amount, polarity) with edit and clear.
- LFOs update at control rate to stay cheap.

### Stage 6: Master page
- Master volume, pan, pitch (range +/-12, 24 or 48 semitones), **Rate** (0.25x to 4x on a log scale, 1.00x at center; scales step speed only, never pitch; in Sync mode it snaps to 1/4x, 1/2x, 1x, 2x, 4x), swing, width, output meter.
- Simple fixed-order effects, each a card with On/Bypass and a mode: Filter (low/band/high pass, notch; cutoff, reso, drive, mix, slope), Delay (free/sync/dotted/triplet; time, feedback, tone, mix, mono or ping-pong), Reverb (room/hall/plate/spring; size, decay, damping, mix, pre-delay), Limiter (soft/hard; ceiling, release, input, mix, lookahead). A bypassed effect uses no CPU.
- Per-track sends to the built-in delay and reverb: BUILT (inside the plug-in only). Two knobs per track in the Sends card under the brass nameplate; they run the Master page Delay and Reverb settings wet-only, and go quiet when that effect is off..
- Master and per-track meters; per-track mute, solo and pan. No separate DAW outputs: Junk Drawer stays one stereo out (decided with Ray).

### Stage 7: Depth
- Per-step pan lane: BUILT (ninth lane tab).
- Per-step probability (Chance lane) and micro-timing (Timing lane, early or late by up to half a step): BUILT. Chance uses fixed dice, so every run and every export plays the same. Rummage and Shake leave these two lanes alone. Loop conditions (every 2nd, 3rd or 4th loop, first loop only) are per track, in the lane Edit menu under "This track plays".
- Per-step filter, pan, and gate/hold.
- Ratchet shapes: BUILT per track (lane Edit menu): Even, Fade out, Swell, Climb. Bounce shape and Ratchet repeats play (100, 75, 50, 25 %) are in the same menu.
- Region order: BUILT (per track, in the lane Edit menu): As written, Walk, Pendulum, Random. "Mostly as written" is the weighted random.
- Region morphing: BUILT. A per-track Morph knob (LFO-assignable) in the Regions card blends each step's region with the next region, equal power; a second voice runs only while Morph is above zero.
- Polymeter lane lengths: BUILT (every lane loops at its own length, set with the Steps buttons; tested).
- Pattern chaining: BUILT. Eight patterns (A-H), a 16-slot chain with 1/2/4/8-bar lengths, changes on the bar line, saved with the project, followed by Export. Song page plus the lane Edit menu.
- Rummage (randomize with locked lanes) and Shake (mutate amount).
- Copy/paste, humanize (volume, length, timing) and reset for lanes: BUILT (the Edit button beside the step lanes). Paste goes into the same kind of lane on any track.

### Key and note matching (BUILT)
- Key found once when a sound loads (pitch-class profile against major and minor; relative keys count as the same). Note found per region on demand (YIN, loudest slices must agree). Both answer "no clear key/note" instead of guessing.
- Project key (Key: none / major / minor) picked from the file strip. Match sets the track's Tune (new per-track parameter, LFO-assignable) to land in the key; Tune on the region tag sets the region Pitch to the nearest note of the key (or nearest semitone with no key). Previews use the track Tune.
- Drawers do not store Tune or the project key (they belong to the sounds).
- Not done: scale beyond major/minor, per-step key follow, chord detection.

## Stage 7b: Export (BUILT: window, first-time folder prompt, offline render; same selections and rules as DriftLooper, with this plug-in's look)
- Export button at the top right of the header opens the Export window (mockup: Export window board).
- WHAT: master stereo file, or one stem per track.
- LENGTH three ways: PASSES (1-16 passes of the longest lane), TIME (minutes:seconds, up to 30 minutes) or BARS (follows tempo and time signature; the window shows the time it comes to). Cap 30 minutes of music per file.
- FILE TYPE WAV / AIFF / FLAC. BIT DEPTH 24-bit or 16-bit (TPDF dither). SAMPLE RATE 96 / 48 / 44.1 kHz.
- ENDING: cut at the end, fade out over 2 / 5 / 10 s (inside the length), or ring out for 3 / 8 s (the sequence stops and the Delay / Reverb tails render after the length).
- NORMALIZE: Off / Relative / Maximum.
- Defaults reproduce DriftLooper's: 4 passes, 96 kHz, 24-bit WAV, cut, master mix.
- A long export is a real offline render with the same engine as live playback: LFOs, sequencer and effects keep moving the whole way (LFOs start from the beginning of their cycle, as on a fresh start). Memory does not grow with length (fixed-size blocks).
- The window shows the estimated size of each file and of all files (FLAC is an estimate) and refuses to start when the drive does not have room.
- Export folder, same flow as DriftLooper (mockup: "Export: first time" board). Stored per computer in settings.xml, mode 0 = not asked yet, 1 = automatic (temporary folder), 2 = a folder you chose.
  - The first time Export is pressed with nothing chosen, a prompt asks "Where should exports go?" with CHOOSE A FOLDER... (opens the system folder picker, starting in Documents) or AUTO-ASSIGN (TEMPORARY FOLDER). Nothing renders until one is picked.
  - The Export window's folder line reads the chosen path, or "Automatic - a temporary folder (cleared by your system now and then)", or "Not chosen yet". CHANGE... opens a small menu: Choose a folder..., Auto-assign (temporary folder), Show the current folder.
  - Every export goes into its own dated sub-folder ("Rotary Dial Export 2026-10-15 21-52-00"). If a chosen folder has gone missing and cannot be recreated, the window says so and opens the picker again.
  - Export settings are Export-window settings, not automatable parameters. Result files can be dragged into a DAW.
- Tests: all file type x bit depth x rate combinations read back correctly (format, length, level, size estimate); bars/time lengths and the 30 minute cap; fade reaches silence without clicks; ring-out keeps tails and ends in silence; 16-bit dither error about 0.7 of a 16-bit step; disk-space refusal; options saved and restored; stems sum to the master mix; LFOs keep moving across a long export.

### Stage 8: Polish
- 100 factory presets ("drawers"), organized by feel. BUILT (generated in code, Presets.h): 10 groups of 10. A drawer keeps every setting except sample files, region counts and region edges. Header bar steps with the arrows and opens a grouped menu; "Save this drawer as..." writes user drawers to the app data folder (Mine). Hosts see them as programs. Rename and delete for saved drawers are in the picker menu. Import and export of .drawer files are in the same menu (imports never overwrite; a clash gets a number). Not done: tuning each factory drawer by ear with real sounds.
- About page and quick start: BUILT (drawer menu, "About and quick start..."). Windows VST3 comes from the GitHub "Windows VST3" workflow on every push to main. Installer deferred.
- macOS build.
- Link/group tracks.
- Animation pass: BUILT, kept small. Playing-step glow and a live dot on each LFO tile (15 Hz, only while shown, off with Reduce motion). More only if it stays cheap.
- Final performance and CPU/RAM audit.

---

## UI and animation

**Layout (1280 x 900 reference)**
- One top-down junk drawer on a counter. Header on the counter: FFA logo and name, page tabs (Drawer, LFOs, Master, Matrix), preset bar, BPM, Host sync, Reduce motion, Settings.
- **Drawer page.** Front board strip: Output (Volume and meter), effect on/off pills, six LFO tiles. Left: eight track compartments (badge, name card, M/S, step dots, meter). Open track: shelf-paper liner with file strip (name, filename, Browse, Record, Normalize), waveform sheet with 4-6 region boxes showing object photos, lane tabs (Region, Volume, Pitch, Ratchet, Direction, Length, Prob., Timing), 16-step organizer with photo chips, and a volume strip. Region tag on the right: Pitch, Gain, Pan, Cutoff, Reso, Drive, Start/End, One-shot/Loop/Ping-pong, Reverse, Snap to hit, and ADSR knobs. Lower compartment: pattern tools (Randomize, Mutate), Catch Last, and the brass FFA nameplate.
- **LFOs page.** Six panels (L1-L6): shape (Sine, Tri, Saw, Sq, S+H, Rand, Drift), Global or Per track, scope display, Rate, Depth, Phase, Fade in, Free/Sync/Dotted/Triplet, Uni/Bi, and a "Hooked up" chip row.
- **Master page.** Master panel (six knobs, stereo meter, pitch range, rate mode) plus four effect cards.
- **Matrix page.** Table of every assignment. (Mockup still to do.)
- **Recording panel.** Input picker, level meter, arm, sync and length, cap per track. (Mockup still to do.)

**Style rules**
- Real photographed wood and objects; turned-wood knobs with a dark tick; brass fittings; dark panels with light text for the top strips.
- Nothing below 11 px text. No overlapping or crowded text; check with real fonts and with wider fallback fonts.
- Every control, including the ADSR knobs, takes an LFO.

**Vocabulary (tooltips and empty states only)**
- Presets are drawers, regions are objects, steps are compartments. These words appear only in tooltips and empty states. LFOs are just called LFOs.
- Randomize is Rummage. Mutate is Shake the drawer. Record is Catch something.
- Empty drawer: "Nothing in here yet. Drop something in."
- Pattern saved: "Put it back where you found it."

**Animation rules (keep it cheap)**
- Animate transforms and opacity only.
- Examples: drawers sliding, objects rattling on a ratchet, LFO tiles wobbling when assigned, a soft glow on the active step, slow-drifting dust in the light.
- Cap at about 30 fps; redraw only what changed; keep audio and UI on separate threads; pause ambient animation when the window is hidden.
- Include a Reduce Motion switch.

---

## Open items

- **Trademark:** look up the two JunkDrawer, Inc. filings (USPTO serials 85631196 and 85703662) to see if they are live. Search software classes for "Junk Drawer," check domain and Gumroad availability, and consider branding as "Junk Drawer by Fading Fractal Audio." A quick attorney check before release is wise.
- **Selling:** Gumroad is the leading option; licensing and activation still undecided (same as DriftLooper).
- **Maximum recording length per track:** decide a cap (60 seconds suggested).
- **UI mockups:** Drawer, LFOs and Master pages approved. Matrix page and Recording panel still to mock up. Published mockup: https://claude.ai/artifact/P7UF5UvDgqiwu8omARZ13H
- **Fade vs ADSR:** the old Fade control was removed; ADSR replaces it.
- **Plugin format:** Windows VST3 plus Standalone first, built with JUCE like DriftLooper.

- **High contrast look (Oct 2026):** a header switch (next to Reduce motion) swaps the warm radio look for black, white and yellow with thick outlines and no glow. Saved with the project and remembered as the default for new instances (a small file in the Rotary Dial app-data folder). The screenshot tool takes JD_HC=1 to render it.
- **Contrast audit:** `JD_AUDIT=1` on the screenshot tool paints every label, finds what is behind it and lists any pair under 4.5:1 (WCAG AA for text). Run it with and without `JD_HC=1` after any color change. Covers the five main pages plus Record, Export and About. Non-text contrast (3:1) is not covered.
- **Keyboard and screen readers (Oct 2026):** Tab reaches the header switches, station bar and dial, waveform, step grid, track rows (open track, level fader), LFO tiles, and every segmented/choice control; arrow keys change values and a focus ring shows where you are (thicker in high contrast). Controls have names, roles and spoken values through JUCE's accessibility layer. Not yet tested with a real screen reader (NVDA/Narrator) or in a DAW on Windows; this is not a compliance claim.

- **Test round 1 (Oct 2026, Ray's Windows notes):** Play and Pause work without the DAW's transport (the pattern runs on its own, and picks up where it stopped); Restart goes back to step 1; the Tempo knob now says what it does (DAW or Own, shows the DAW's tempo while following it); Delay Time reads in beats in Sync, Dotted and Triplet; Undo and Redo (header buttons, Ctrl+Z and Ctrl+Y; one drag is one step; loaded sounds are not part of it); Randomize, Clear lane and Clear all under the lane tabs; one step count for every lane of a track; bigger Delay and Reverb send knobs and a bigger Tune knob; bigger text everywhere (nothing under 13 px; Master, LFOs, Matrix, Song and Export larger still); the animation keeps running after switching High contrast on and back (tests check it). The screenshot tool now also reports any text that does not fit its box.
