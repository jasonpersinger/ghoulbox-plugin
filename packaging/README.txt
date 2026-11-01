GHOULBOX @VERSION@ for Linux, by MESSYQUEST
Dungeon synth for your DAW: the GHOULBOX firmware for the M-Vave FM-1, as a CLAP, VST3 and LV2 instrument.
https://ghoulbox.messy.quest

INSTALL
  Open a terminal in this folder and run:   ./install.sh
  Then rescan plugins in your DAW. In REAPER: Options > Preferences > Plug-ins > CLAP (or VST) > Re-scan.
  Insert "GHOULBOX (MESSYQUEST)". In REAPER, the CLAP version is listed as "CLAPi: GHOULBOX (MESSYQUEST)".
  To remove it:                             ./install.sh --uninstall

  Or by hand: copy CLAP/ghoulbox.clap together with CLAP/libghoulbox_core.so into ~/.clap, VST3/ghoulbox.vst3
  into ~/.vst3, LV2/ghoulbox.lv2 into ~/.lv2. Each format needs its libghoulbox_core.so beside it.

  Needs: 64-bit Linux with glibc 2.34 or newer (Ubuntu 22.04, Debian 12, Fedora 35, or anything newer), X11 or
  XWayland, OpenGL. Tested in REAPER. Use the CLAP version where your DAW has it: the VST3 version shows about
  2,000 extra MIDI controller entries in generic parameter lists (a VST3 quirk).

PLAYING IT
  GHOULBOX has four layered sound slots. Incoming MIDI plays every enabled slot at once, so one keyboard
  plays a stack of up to four sounds. Slot 1 is on by default; toggle slots on and off with the ENABLED/OFF
  button on each slot across the bottom.
  Voices: GHOULBOX plays 12 voices in all, shared by the four slots, as on the device. Each enabled slot uses
  one voice per note, so with four slots on, three-note chords use all 12; further notes take over the oldest
  voices. One slot on gives 12-note polyphony.
  MIDI IN (MENU > MIDI): the slots take MIDI from every channel whatever this setting says. It is kept in your
  project as you set it.
  It follows your DAW's tempo. Your project saves GHOULBOX's sounds and settings; SAVE slots are shared by every
  GHOULBOX on your computer (~/.local/share/GHOULBOX).
  In the window, right-click a button to hold it down (for combinations). The computer keyboard plays the device
  too once you click its screen, if your DAW passes keys to plugins (REAPER: the FX window's + menu > Send all
  keyboard input to plug-in).

KNOWN ISSUES
  Latency: GHOULBOX handles MIDI in steps of about 2.9 ms, so the start of a note can vary by up to about 3 ms from
  one note to the next. Each sound also has its own attack time. At host sample rates other than 44.1 kHz the sound
  starts a further delay later (measured): about 1.45 ms at 48, 88.2, 96, 176.4 and 192 kHz, about 2 ms at 32 kHz
  and about 2.9 ms at 22.05 kHz. GHOULBOX does not report latency to your DAW, so your DAW does not compensate for
  it.
  MIDI: if a host sends more than 512 MIDI events in one audio block, events past the 512th are dropped by the
  plug-in framework. If a note-off is among them, the note may keep sounding.

SUPPORT
  support@messy.quest

LICENCE
  Copyright (C) 2026 MESSYQUEST.
  GHOULBOX is free software under the GNU General Public License, version 3 (LICENSES/GPL-3.0.txt). Its complete
  source code is included: ghoulbox-@VERSION@-source.tar.gz. The licences of the parts it includes are in LICENSES/.
  GHOULBOX is an independent project. It is not affiliated with, endorsed by or sponsored by M-Vave. The names
  M-Vave and FM-1 are used only to identify the hardware the GHOULBOX firmware was written for.
