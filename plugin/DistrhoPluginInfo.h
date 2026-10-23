/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "MESSYQUEST"                  /* placeholder name: final in Plan 3 */
#define DISTRHO_PLUGIN_NAME    "GHOULBOX"
/* permanent ids: never change them (saved projects find the plugin by them; VST3's UID is BRAND_ID + UNIQUE_ID) */
#define DISTRHO_PLUGIN_URI     "https://ghoulbox.messy.quest/plugin"
#define DISTRHO_PLUGIN_CLAP_ID "quest.messy.ghoulbox"
#define DISTRHO_PLUGIN_BRAND_ID  MsyQ
#define DISTRHO_PLUGIN_UNIQUE_ID GhBx

#define DISTRHO_PLUGIN_HAS_UI           0
#define DISTRHO_PLUGIN_IS_SYNTH         1
#define DISTRHO_PLUGIN_NUM_INPUTS       0
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1

#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:InstrumentPlugin"

#endif
