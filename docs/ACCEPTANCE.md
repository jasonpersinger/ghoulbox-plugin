# Plan 1 acceptance in REAPER (owner)

Build: `plan1-headless` (installed with `make install`: `~/.clap`, `~/.vst3`, `~/.lv2`).
Setup: REAPER on CachyOS, PulseAudio backend, `PULSE_LATENCY_MSEC=20`. Use the **CLAP** version unless a step says otherwise.
For each step write **PASS** or **FAIL**, plus anything you heard or saw.

The plugin has no window yet (that's Plan 2). REAPER shows its controls as a generic list of sliders:
**Master**, **Host sync**, then 8 per track (**Level, Pan, Reverb, Delay, Knob 1–4**). A track slider at **—**
(its lowest setting) means "leave it alone": the project's own value stands until you move it.

| # | Step | Pass when | Result | Notes |
|---|------|-----------|--------|-------|
| 1 | Rescan plugins (Options > Preferences > Plug-ins > CLAP > Re-scan). Insert GHOULBOX (CLAP) on a track armed for MIDI from the Launchkey. Play. | Notes play GHOULBOX's sounds; no splash or intro noise when it's inserted. | | |
| 2 | Two GHOULBOX instances on two tracks. Give each a different sound by moving its *Track 1 Knob* sliders. | Neither affects the other. | | |
| 3 | Start REAPER's transport with GHOULBOX's sequencer pattern set (a project saved on the FM-1 loaded via state, or the default scene). Try 90 and 140 BPM, then a 2-bar loop region. | Plays in time at both tempos and loops correctly. | | |
| 3b | Same as 3, with the project in 6/8 time (CLAP). | Plays in time, not at half speed, and doesn't keep restarting. | | |
| 4 | Save the REAPER project, close it, reopen it. | The same sound and pattern. | | |
| 5 | Automate *Track 1 Knob 1* with an envelope. | Audible and smooth. | | |
| 6 | Note CPU per instance from REAPER's Performance Meter (View > Performance Meter). | Write the % here. Above 5 % of one core opens a follow-up (risk 5: skip screen drawing while no window is open). | | |

Known limits in this build:
- A loop or jump restarts GHOULBOX's pattern at step 1 (the firmware takes no Song Position Pointer).
- Pressing SAVE needs the window (Plan 2). The shared SAVE library is `~/.local/share/GHOULBOX/library.nor`.
  Each instance's autosave (the firmware's "restore last") stays its own and never reaches the shared library:
  a new instance always starts from the default music, and the REAPER project keeps each instance's music.
- Tempo sync in x/8 time is fixed and tested for CLAP and VST3; the LV2 version in x/8 is untested.

Measured by the builder (not REAPER): one core at about 0.5 % of a CPU core with a note held (204× real time).
