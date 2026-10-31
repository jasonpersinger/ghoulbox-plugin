# Changelog

## [0.2.0] - 2026-10-30

### Breaking Changes
- **Project compatibility break**: DAW projects saved with GHOULBOX 0.1.0 do not carry over to 0.2.0. Parameters have been cleanly restructured and renumbered.
- **Removed DREAM mode**: Generative DREAM playback and its parameters (Evolve, Density, Drift, Mood) are removed.
- **Removed internal sequencer**: Pattern sequencing, step grid, and autonomous transport playback (SEQ, PLAY, REC) are removed.

### Added
- **4 Layered Sound Slots**: Up to 4 layered sound slots per instance. Incoming MIDI on any channel plays every enabled slot at once. Slot 1 is enabled by default; slots 2–4 are off by default.
- **Silent on load**: New instances boot with clean, empty patterns and produce no sound when DAW transport plays without incoming MIDI notes.
- **Spacious window UI**: Replaced step grid and narrow track columns with 4 spacious slot strips featuring slot selection, ON/OFF enable toggles, sound name display, and volume faders.
- **Clean parameter layout**: Structured into Master (0), per-slot Enabled/Level/Pan/Reverb/Delay/Knob 1–4 (1–36), and Reverb Type / Tape (37–38).
