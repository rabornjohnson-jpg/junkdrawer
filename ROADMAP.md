# Junk Drawer: Roadmap

A Fading Fractal Audio plugin. An 8-track sample sequencer: cut each sample into 4-6 regions, then let every step choose which piece plays.

**Pitch:** Chop any sound into regions and let the sequencer decide which piece plays on every step.

Status: concept locked (Oct 2026). Name confirmed: Junk Drawer. Run a trademark check before release (see Open Items).

---

## Identity

- **Family feel:** melancholy, warm, but fun. Same Fading Fractal Audio logo and name in the UI header.
- **Concept:** a real junk drawer pulled open. Warm wood, tarnished brass, dusty amber light, objects that look well used.
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
- Eight-drawer shell: closed drawers show a label and a tiny level meter; one drawer open at a time.
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

### Stage 4: Recording
- Per-track Record button, capturing from the plugin's audio input.
- Sync options: immediate, next bar, or fixed length (1, 2, 4, 8 bars). Short pre-roll buffer.
- Replace or overdub.
- Always-on rolling buffer ("Catch Last") that can be dragged into any track.
- Waveform draws live while recording. Option to save a recording to file.
- Per-track maximum length to cap memory use. Report latency correctly.
- Feedback warning when recording the plugin's own output.

### Stage 5: LFOs
- Six LFOs: sine, triangle, saw, square, sample and hold, smooth random, plus a slow "drift" shape.
- Rate (free or tempo-synced, including dotted and triplet), depth, phase, polarity, restart mode.
- Each LFO can be global or per-track. LFO-to-LFO modulation allowed.
- Many-to-many assignments, drawn as rubber bands from LFO to control, with thickness for amount.
- Mod matrix view listing every assignment.
- LFOs update at control rate to stay cheap.

### Stage 6: The Countertop (master section)
- Master volume, pan, global pitch offset, global step length/rate, swing, width/mono.
- Simple fixed-order effects: filter, delay, small reverb, soft clipper or limiter.
- Per-track sends to delay and reverb.
- Master and per-track meters; per-track mute, solo, pan, and output routing to separate outs.

### Stage 7: Depth
- Per-step probability, micro-timing, and conditions ("every 3rd loop").
- Per-step filter, pan, and gate/hold.
- Ratchet shapes (rising, falling, bouncing) and ratchet probability.
- Region modes: sequential, random, pendulum, weighted random.
- Region morphing (crossfade between two regions on one step).
- Polymeter lane lengths.
- Pattern chaining (8-16 patterns, changes on bar boundaries).
- Rummage (randomize with locked lanes) and Shake (mutate amount).
- Copy/paste and humanize for lanes.

### Stage 8: Polish
- 100 factory presets ("drawers"), organized by feel.
- Installer, About page, quick-start guide.
- macOS build.
- Link/group tracks.
- Animation pass (see below).
- Final performance and CPU/RAM audit.

---

## UI and animation

**Layout**
- Eight stacked drawer fronts; click one to slide it open.
- Open drawer: the sample as a long waveform tray with 4-6 colorful objects (key, button, spool, rubber band, battery, bottle cap) sitting on it. Each object is a region; drag its edges to set start and end.
- Step lane underneath as a row of small compartments. Drag an object into a compartment to place it.
- Clicking an object opens a small tag with its region controls.
- Countertop above the drawers holds the master controls, effects, and meters.

**Vocabulary (tooltips and empty states only)**
- Tracks are drawers, regions are objects, steps are compartments, LFOs are rubber bands, master section is the countertop, presets are drawers.
- Randomize is Rummage. Mutate is Shake the drawer. Record is Catch something.
- Empty drawer: "Nothing in here yet. Drop something in."
- Pattern saved: "Put it back where you found it."

**Animation rules (keep it cheap)**
- Animate transforms and opacity only.
- Examples: drawers sliding, objects rattling on a ratchet, rubber bands wobbling when assigned, a soft glow on the active step, slow-drifting dust in the light.
- Cap at about 30 fps; redraw only what changed; keep audio and UI on separate threads; pause ambient animation when the window is hidden.
- Include a Reduce Motion switch.

---

## Open items

- **Trademark:** look up the two JunkDrawer, Inc. filings (USPTO serials 85631196 and 85703662) to see if they are live. Search software classes for "Junk Drawer," check domain and Gumroad availability, and consider branding as "Junk Drawer by Fading Fractal Audio." A quick attorney check before release is wise.
- **Selling:** Gumroad is the leading option; licensing and activation still undecided (same as DriftLooper).
- **Maximum recording length per track:** decide a cap (60 seconds suggested).
- **UI mockups:** draft the open-drawer screen and approve it before building.
