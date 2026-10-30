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

## Plan 2: the window

Use the **CLAPi** version. Open GHOULBOX's window from the FX chain. Hardware controls by mouse: a left click presses a
button while held; a **right click latches** it held (red outline) until you click it again, so you can hold one
button and turn a dial (HARDWARE, right column: drag up/down or use the wheel). The computer keyboard plays the device
like the browser emulator (Z row and A row: keys; number row: buttons; [ ]: OCT; arrows: SELECT / ALGORITHM) once
you click the screen, and only if the FX window's **+ menu > Send all keyboard input to plug-in** is on.

| # | Step | Pass when | Result | Notes |
|---|------|-----------|--------|-------|
| 1 | Open the window. Drag its corner to make it about 0.75x and 1.5x the size. | Opens within 1 s showing the live HOME screen, none of it cut off at the right or bottom; reads clearly at every size; the mouse still hits what's under it. | | |
| 2 | Hold HOME (right-click latch) to open the MENU, step with PRESETS (HARDWARE dial) and ALGORITHM to SYSTEM > CALIBRATION, press OCT+. | "NOT IN PLUGIN" appears; audio carries on; REAPER doesn't freeze. | | |
| 3 | Click a sound in the list; drag each of the 4 knobs, a track fader, DENS, TAPE and VOL; click EVOL and TYPE. | Each changes the sound, and the screen's pages agree (GLO for DREAM, FX for TYPE and TAPE). | | |
| 4 | Track in Write automation mode: drag Track 1 Knob 1 in the window while playing. Then play it back in Read mode. | An envelope appears and plays back. | | |
| 5 | Select T3 in TRACKS. | The knobs, the sound list's heading and the steps switch to track 3. | | |
| 6 | Press play in REAPER with a pattern set. | The step grid's red marker follows playback; the top bar says PLAYING and SYNCED. | | |
| 7 | Two instances, both windows open. | Each window shows and controls only its own GHOULBOX. | | |
| 8 | Performance Meter: CPU per instance with the window open, then closed. | Write both % here. | | |
| 9 | Your look check: colours, sizes, wording. | Note anything to change; each becomes a follow-up. | | |

Known limits of the window:
- The step grid shows the steps; editing them is on the device's screen (SEQ).
- MENU > SYSTEM's USB SERIAL and USB LEVEL do nothing in the plugin; CALIBRATION is refused; the battery shows the
  external-power bolt.
- Per-track outputs and dragging steps into REAPER as MIDI come in version 2.
