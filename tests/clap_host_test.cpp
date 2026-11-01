/* SPDX-License-Identifier: GPL-3.0-only */
/* Task 7: the real plugin (build/plugin/ghoulbox.clap) in a minimal CLAP host: it plays, instances stay apart, its state
 * comes back and garbage is refused, the shared library is written, nothing is left in the temp folder. */
#include "clap/entry.h"
#include "clap/ext/gui.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/host.h"
#include "clap/plugin-factory.h"
#include <chrono>
#include "private_runtime_dir.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; return; } } while (0)

static const char* PATH = "build/plugin/ghoulbox.clap";
static const double RATE = 48000;
static const clap_plugin_factory_t* factory;

static const void* hostExt(const clap_host_t*, const char*) { return nullptr; }
static void hostNop(const clap_host_t*) {}
static const clap_host_t host = {CLAP_VERSION, nullptr, "test", "", "", "1", hostExt, hostNop, hostNop, hostNop};

struct Events {                                       /* an input event list: CLAP MIDI events */
    std::vector<clap_event_midi_t> ev;
    clap_input_events_t list{this, size, get};
    static uint32_t size(const clap_input_events_t* l) { return (uint32_t)((Events*)l->ctx)->ev.size(); }
    static const clap_event_header_t* get(const clap_input_events_t* l, uint32_t i) { return &((Events*)l->ctx)->ev[i].header; }
};
static bool pushNop(const clap_output_events_t*, const clap_event_header_t*) { return true; }
static const clap_output_events_t outEvents = {nullptr, pushNop};

struct Inst {
    const clap_plugin_t* p = nullptr;
    uint64_t t = 0;
    explicit Inst(const clap_plugin_factory_t* f = factory)
    {
        p = f->create_plugin(f, &host, "quest.messy.ghoulbox");
        if (p && p->init(p) && p->activate(p, RATE, 1, 4096) && p->start_processing(p))
            return;
        p = nullptr;
    }
    ~Inst()
    {
        if (p) {
            p->stop_processing(p);
            p->deactivate(p);
            p->destroy(p);
        }
    }
    /* n frames (one block); its peak. midi: one message at frame 0 */
    float run(uint32_t n, const uint8_t* midi = nullptr, const clap_input_events_t* extra = nullptr,
              bool transportPlaying = false, double bpm = 120.0)
    {
        std::vector<float> l(n), r(n);
        float* ch[2] = {l.data(), r.data()};
        clap_audio_buffer_t out{};
        out.data32 = ch;
        out.channel_count = 2;
        Events in;
        if (midi) {
            clap_event_midi_t e{};
            e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
            std::memcpy(e.data, midi, 3);
            in.ev.push_back(e);
        }
        clap_event_transport_t tr{};
        tr.header = {sizeof tr, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
        tr.flags = transportPlaying ? CLAP_TRANSPORT_IS_PLAYING : 0;
        tr.song_pos_beats = (clap_beattime)(t * bpm / (60.0 * RATE) * CLAP_BEATTIME_FACTOR);
        tr.tempo = bpm;
        tr.tsig_num = 4;
        tr.tsig_denom = 4;
        clap_process_t pr{};
        pr.steady_time = (int64_t)t;
        pr.frames_count = n;
        pr.transport = &tr;
        pr.audio_outputs = &out;
        pr.audio_outputs_count = 1;
        pr.in_events = extra ? extra : &in.list;
        pr.out_events = &outEvents;
        p->process(p, &pr);
        t += n;
        float pk = 0;
        for (uint32_t i = 0; i < n; i++)
            pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
        return pk;
    }
    float runFor(double s, const uint8_t* midi = nullptr, bool transportPlaying = false, double bpm = 120.0)
    {
        float pk = run(512, midi, nullptr, transportPlaying, bpm);
        for (uint32_t k = 512; k < s * RATE; k += 4096)
            pk = std::max(pk, run(4096, nullptr, nullptr, transportPlaying, bpm));
        return pk;
    }
    std::string save()
    {
        auto* st = (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
        std::string s;
        clap_ostream_t os{&s, [](const clap_ostream_t* o, const void* b, uint64_t n) -> int64_t {
                              ((std::string*)o->ctx)->append((const char*)b, n);
                              return (int64_t)n;
                          }};
        return st && st->save(p, &os) ? s : "";
    }
    bool load(const std::string& s)
    {
        auto* st = (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
        struct R { const std::string* s; size_t at; } r{&s, 0};
        clap_istream_t is{&r, [](const clap_istream_t* i, void* b, uint64_t n) -> int64_t {
                              R* r = (R*)i->ctx;
                              n = std::min<uint64_t>(n, r->s->size() - r->at);
                              std::memcpy(b, r->s->data() + r->at, n);
                              r->at += n;
                              return (int64_t)n;
                          }};
        return st && st->load(p, &is);
    }
};

/* the "project" value inside a DPF state stream (key and value end in a NUL on the wire) */
static const std::string KEY("project\0", 8);
static std::string project(const std::string& s)
{
    size_t a = s.find(KEY);
    if (a == std::string::npos)
        return "";
    a += KEY.size();
    return s.substr(a, s.find('\0', a) - a);
}
static std::string withProject(std::string s, const std::string& v)
{
    size_t a = s.find(KEY) + KEY.size();
    return s.replace(a, s.find('\0', a) - a, v);
}

static const uint8_t NOTE_ON[3] = {0x90, 60, 100};

static void test_plays_a_note()
{
    Inst a;
    CHECK(a.p);
    CHECK(a.runFor(0.1) < 1e-4f);                     /* no splash noise on insert */
    CHECK(a.runFor(0.2, NOTE_ON) > 0.01f);
}

static void test_fresh_transport_silent()
{
    Inst a;
    CHECK(a.p);
    /* 2 seconds of transport playing with no MIDI notes: peak output stays at tape noise floor
     * (default TAPE 40 hiss peak is 5.95e-5 = -84.5 dBFS; no notes or patterns sound) */
    CHECK(a.runFor(2.0, nullptr, true, 120.0) < 6e-5f);
    /* Then one note on sounds */
    CHECK(a.runFor(0.2, NOTE_ON, true, 120.0) > 0.01f);
}

static void test_two_instances_apart()
{
    Inst a, b;
    CHECK(a.p && b.p);
    CHECK(a.runFor(0.2, NOTE_ON) > 0.01f);
    CHECK(b.runFor(0.2) < 1e-4f);
}

static void test_state_roundtrip()
{
    Inst a;
    CHECK(a.p);
    const uint8_t cc[3] = {0xB0, 21, 5};              /* a knob moved: the music differs from the default */
    a.runFor(0.2, cc);
    std::string s = a.save();
    CHECK(!project(s).empty());
    Inst fresh;
    CHECK(project(fresh.save()) != project(s));
    Inst b;
    CHECK(b.load(s));
    b.runFor(0.1);
    CHECK(project(b.save()) == project(s));
}

static void test_state_rejects_garbage()
{
    Inst a, fresh;
    CHECK(a.p && fresh.p);
    a.runFor(0.1);
    fresh.runFor(0.1);                                /* (its parameters applied: Host sync sets the project's CLOCK) */
    std::string s = a.save(), def = project(fresh.save());
    CHECK(!def.empty() && project(s) == def);
    for (const std::string& bad : {std::string("AAAAAAAA"), project(s).substr(0, project(s).size() / 2), std::string("!!")}) {
        Inst b;
        CHECK(b.p);
        b.load(withProject(s, bad));                  /* the host may say ok: the music must stay the default */
        b.runFor(0.1);
        CHECK(project(b.save()) == def);
        CHECK(b.runFor(0.2, NOTE_ON) > 0.01f);        /* and it still plays */
    }
}

static std::string libFile() { return std::string(std::getenv("XDG_DATA_HOME")) + "/GHOULBOX/library.nor"; }
static void test_library_written()
{
    struct stat st;
    CHECK(stat(libFile().c_str(), &st) != 0);         /* nothing yet */
    Inst a;
    CHECK(a.p);
    const uint8_t cc[3] = {0xB0, 22, 99};
    a.runFor(0.2, cc);
    a.runFor(15);                                     /* the firmware's autosave: a change, then 10 s quiet */
    bool written = false;
    for (int i = 0; i < 40 && !written; i++) {        /* the worker commits within a second or so */
        written = stat(libFile().c_str(), &st) == 0 && st.st_size == 0x100000;
        if (!written)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    CHECK(written);
}

/* the firmware's autosave is the instance's own current music (the DAW project keeps it): another instance's must
 * never become a new instance's music through the shared library */
static void test_autosave_stays_private()
{
    std::string keep = std::getenv("XDG_DATA_HOME"), own = keep + "/autosave";
    setenv("XDG_DATA_HOME", own.c_str(), 1);
    std::string def;
    {
        Inst fresh;
        CHECK(fresh.p);
        fresh.runFor(0.1);
        def = project(fresh.save());
        CHECK(!def.empty());
    }
    {
        Inst a;
        CHECK(a.p);
        const uint8_t cc[3] = {0xB0, 23, 77};
        a.runFor(0.2, cc);
        a.runFor(15);                                 /* autosaved: a change, then 10 s quiet */
        CHECK(project(a.save()) != def);
    }                                                 /* (destroyed: its flash committed) */
    struct stat st;
    CHECK(stat((own + "/GHOULBOX/library.nor").c_str(), &st) == 0);
    {
        Inst b;
        CHECK(b.p);
        b.runFor(0.1);
        CHECK(project(b.save()) == def);              /* a new instance: the default music, not A's */
    }
    setenv("XDG_DATA_HOME", keep.c_str(), 1);
}

/* the plugin without its core (a bad install): silent, and the project's music is kept, never saved over */
static const clap_plugin_factory_t* noCoreFactory()
{
    static const clap_plugin_factory_t* f;
    if (f)
        return f;
    std::string dir = std::string(std::getenv("XDG_DATA_HOME")) + "/nocore", bin = dir + "/ghoulbox.clap";
    mkdir(dir.c_str(), 0755);
    if (std::system(("cp " + std::string(PATH) + " " + bin).c_str()) != 0)
        return nullptr;
    void* so = dlopen(bin.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto* e = so ? (const clap_plugin_entry_t*)dlsym(so, "clap_entry") : nullptr;
    if (e && e->init(bin.c_str()))
        f = (const clap_plugin_factory_t*)e->get_factory(CLAP_PLUGIN_FACTORY_ID);
    return f;
}
static void test_missing_core_keeps_state()
{
    Inst a;
    CHECK(a.p);
    const uint8_t cc[3] = {0xB0, 21, 9};
    a.runFor(0.2, cc);
    std::string s = a.save();
    CHECK(!project(s).empty());
    auto* f = noCoreFactory();
    CHECK(f);
    Inst n(f);
    CHECK(n.p);
    CHECK(n.runFor(0.2, NOTE_ON) == 0.f);             /* silent, no crash */
    CHECK(n.load(s));
    CHECK(project(n.save()) == project(s));           /* what it was given, given back */
}

/* Plan 3: clean parameter list (39 parameters: Master, 4x9 Slot params, Reverb Type, Tape) */
static void test_param_list()
{
    Inst a;
    CHECK(a.p);
    auto* pa = (const clap_plugin_params_t*)a.p->get_extension(a.p, CLAP_EXT_PARAMS);
    CHECK(pa && pa->count(a.p) == 39);
    std::vector<std::string> want = {"Master"};
    for (int t = 1; t <= 4; t++) {
        want.push_back("Slot " + std::to_string(t) + " Enabled");
        for (const char* k : {"Level", "Pan", "Reverb", "Delay", "Knob 1", "Knob 2", "Knob 3", "Knob 4"})
            want.push_back("Slot " + std::to_string(t) + " " + k);
    }
    for (const char* k : {"Reverb Type", "Tape"})
        want.push_back(k);
    for (uint32_t i = 0; i < 39; i++) {
        clap_param_info_t info{};
        CHECK(pa->get_info(a.p, i, &info));
        if (want[i] != info.name)
            std::fprintf(stderr, "  param %u: %s, want %s\n", i, info.name, want[i].c_str());
        CHECK(want[i] == info.name);
    }

    // Check defaults and ranges
    clap_param_info_t info{};
    CHECK(pa->get_info(a.p, 0, &info));
    CHECK(info.default_value == 800 && info.min_value == 0 && info.max_value == 1023);

    for (int t = 0; t < 4; t++) {
        CHECK(pa->get_info(a.p, 1 + t * 9, &info));
        CHECK(info.default_value == (t == 0 ? 1.0 : 0.0) && info.min_value == 0 && info.max_value == 1);
        CHECK((info.flags & CLAP_PARAM_IS_STEPPED) != 0);

        for (int k = 1; k < 9; k++) {
            CHECK(pa->get_info(a.p, 1 + t * 9 + k, &info));
            CHECK(info.default_value == -1.0 && info.min_value == -1 && info.max_value == 127);
        }
    }

    CHECK(pa->get_info(a.p, 37, &info));
    CHECK(info.default_value == -1.0 && info.min_value == -1 && info.max_value == 2);
    CHECK(pa->get_info(a.p, 38, &info));
    CHECK(info.default_value == -1.0 && info.min_value == -1 && info.max_value == 127);
}

/* A global parameter (Reverb Type / Tape) reaches the music (the project's state changes) */
static void test_global_param_changes_state()
{
    Inst a;
    CHECK(a.p);
    auto* pa = (const clap_plugin_params_t*)a.p->get_extension(a.p, CLAP_EXT_PARAMS);
    clap_param_info_t info{};
    CHECK(pa && pa->get_info(a.p, 37, &info));         /* param 37: Reverb Type */
    a.runFor(0.1);
    const std::string before = project(a.save());
    struct P {
        clap_event_param_value_t e{};
        clap_input_events_t list{this, [](const clap_input_events_t*) -> uint32_t { return 1; },
                                 [](const clap_input_events_t* l, uint32_t) -> const clap_event_header_t* { return &((P*)l->ctx)->e.header; }};
    } ev;
    ev.e.header = {sizeof ev.e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    ev.e.param_id = info.id;
    ev.e.note_id = -1;
    ev.e.port_index = ev.e.channel = ev.e.key = -1;
    ev.e.value = 1;                                     /* SPRING */
    a.run(512, nullptr, &ev.list);
    a.runFor(0.1);
    const std::string after = project(a.save());
    CHECK(!before.empty() && !after.empty());
    CHECK(before != after);
}

/* Plan 2: the plugin has a window (X11, embedded) */
static void test_gui_extension()
{
    Inst a;
    CHECK(a.p);
    auto* gui = (const clap_plugin_gui_t*)a.p->get_extension(a.p, CLAP_EXT_GUI);
    CHECK(gui);
    CHECK(gui->is_api_supported(a.p, CLAP_WINDOW_API_X11, false));
}

/* a block's input events: parameter values, then one MIDI message, all at frame 0 */
struct MixedEvents {
    union E { clap_event_header_t h; clap_event_param_value_t p; clap_event_midi_t m; };
    std::vector<E> ev;
    clap_input_events_t list{this, [](const clap_input_events_t* l) -> uint32_t { return (uint32_t)((MixedEvents*)l->ctx)->ev.size(); },
                             [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* {
                                 return &((MixedEvents*)l->ctx)->ev[i].h; }};
    void param(clap_id id, double v)
    {
        E e{};
        e.p.header = {sizeof e.p, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.p.param_id = id;
        e.p.note_id = e.p.port_index = e.p.channel = e.p.key = -1;
        e.p.value = v;
        ev.push_back(e);
    }
    void midi(const uint8_t* b)
    {
        E e{};
        e.m.header = {sizeof e.m, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        std::memcpy(e.m.data, b, 3);
        ev.push_back(e);
    }
    void midiAt(uint32_t frame, const uint8_t* b)
    {
        E e{};
        e.m.header = {sizeof e.m, frame, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        std::memcpy(e.m.data, b, 3);
        ev.push_back(e);
    }
};
static clap_id paramId(const Inst& a, uint32_t index)
{
    auto* pa = (const clap_plugin_params_t*)a.p->get_extension(a.p, CLAP_EXT_PARAMS);
    clap_param_info_t info{};
    return pa && pa->get_info(a.p, index, &info) ? info.id : CLAP_INVALID_ID;
}

/* MIDI slot layering: a note on channel 16 plays slot 1 by default; enabling slot 3 plays both; all off is silent */
static void test_layer_plays_any_channel()
{
    static const uint8_t NOTE16[3] = {0x9F, 60, 100};
    Inst on;
    CHECK(on.p);
    on.runFor(0.1);
    CHECK(on.runFor(0.2, NOTE16) > 0.01f);             // slot 1 is enabled by default: plays

    MixedEvents evBoth;
    evBoth.param(paramId(on, 19), 1);                // enable slot 3 as well (param 1 + 2*9 = 19)
    evBoth.midi(NOTE16);
    on.run(512, nullptr, &evBoth.list);
    CHECK(on.runFor(0.2) > 0.01f);

    Inst off;
    CHECK(off.p);
    off.runFor(0.1);
    MixedEvents evOff;
    evOff.param(paramId(off, 1), 0);                  // disable slot 1 (param 1)
    off.run(512, nullptr, &evOff.list);
    off.runFor(0.1);
    CHECK(off.runFor(0.2, NOTE16) < 1e-4f);           // all slots off: silent
}

/* Bug 1: Saved firmware edits must not be overwritten during restore */
static void test_saved_firmware_edits_preserved_on_restore()
{
    Inst a;
    CHECK(a.p);
    // Set Slot 1 Knob 1 (param index 6: 1 + 0*9 + 5 = 6) through host parameter to 10
    MixedEvents evParam;
    evParam.param(paramId(a, 6), 10);
    a.run(512, nullptr, &evParam.list);
    a.runFor(0.1);

    // Change Slot 1 Knob 1 through MIDI CC 21 to 100
    const uint8_t cc21[3] = {0xB0, 21, 100};
    a.runFor(0.1, cc21);

    // Save with UI closed
    std::string s = a.save();
    CHECK(!project(s).empty());

    // Reload into a fresh instance
    Inst b;
    CHECK(b.load(s));
    b.runFor(0.1);

    // Check that restored firmware state preserves the CC value (100) and was not overwritten by stale 10
    std::string sRestored = b.save();
    CHECK(project(sRestored) == project(s));
}

/* Bug 2: Disabling a slot must silence held notes and sustain without leaving stranded voices */
static void test_disable_slot_silences_held_notes()
{
    Inst a;
    CHECK(a.p);
    const uint8_t noteOn[3] = {0x90, 60, 100};
    const uint8_t noteOff[3] = {0x80, 60, 0};

    // Hold note on Slot 1
    a.run(512, noteOn);
    CHECK(a.runFor(0.2) > 0.05f);

    // Disable slot 1 (param index 1)
    MixedEvents evDis;
    evDis.param(paramId(a, 1), 0);
    a.run(512, nullptr, &evDis.list);

    // Send note-off after disabling
    a.run(512, noteOff);

    // Let the release decay for 3 seconds, then measure peak:
    a.runFor(3.0);
    CHECK(a.run(4096) < 0.005f);
}

/* Bug 3: Channel remapping retains overlapping-note ownership */
static void test_overlapping_note_ownership_clap()
{
    Inst a;
    CHECK(a.p);
    const uint8_t onCh1[3] = {0x90, 60, 100};
    const uint8_t onCh2[3] = {0x91, 60, 100};
    const uint8_t offCh1[3] = {0x80, 60, 0};
    const uint8_t offCh2[3] = {0x81, 60, 0};

    // Hold C4 on channel 1 and channel 2
    a.run(512, onCh1);
    a.run(512, onCh2);
    CHECK(a.runFor(0.1) > 0.05f);

    // Release only channel 1: channel 2 is still held, so sound must continue
    a.run(512, offCh1);
    CHECK(a.runFor(0.2) > 0.01f);

    // Release channel 2: now all owners released, sound should decay to silent
    a.run(512, offCh2);
    a.runFor(3.0);
    CHECK(a.run(4096) < 0.005f);
}

/* Bug 4: Event truncation must guarantee note-off delivery */
static void test_truncation_preserves_note_off()
{
    Inst a;
    CHECK(a.p);
    // Enable all 4 slots (params 1, 10, 19, 28)
    MixedEvents evEn;
    evEn.param(paramId(a, 1), 1);
    evEn.param(paramId(a, 10), 1);
    evEn.param(paramId(a, 19), 1);
    evEn.param(paramId(a, 28), 1);
    a.run(512, nullptr, &evEn.list);

    // Play a note
    const uint8_t noteOn[3] = {0x90, 60, 100};
    a.run(512, noteOn);
    CHECK(a.runFor(0.2) > 0.05f);

    // Send 256 spaced CC events followed by Note Off in one 4096-frame block
    MixedEvents evDense;
    for (uint32_t i = 0; i < 256; i++) {
        uint8_t cc[3] = {0xB0, 21, (uint8_t)(i & 127)};
        evDense.midiAt(i * 15, cc);
    }
    const uint8_t noteOff[3] = {0x80, 60, 0};
    evDense.midiAt(4000, noteOff);

    a.run(4096, nullptr, &evDense.list);

    // Let the release decay, then measure peak:
    a.runFor(4.0);
    CHECK(a.run(4096) < 0.005f);
}

static int coreTemps()
{
    const char* x = std::getenv("XDG_RUNTIME_DIR");
    int n = 0;
    if (DIR* d = opendir(x && *x ? x : "/tmp")) {
        while (dirent* e = readdir(d))
            n += std::strncmp(e->d_name, "ghoulbox-core-", 14) == 0;
        closedir(d);
    }
    return n;
}
static void test_no_temp_leftovers()
{
    for (int i = 0; i < 10; i++) {
        Inst a;
        CHECK(a.p);
    }
    CHECK(coreTemps() == 0);
}

int main()
{
    PrivateRuntimeDir rt;
    char tmpl[] = "build/plugtest-XXXXXX";
    if (!mkdtemp(tmpl))
        return 2;
    std::string data = std::string(realpath(tmpl, nullptr));
    setenv("XDG_DATA_HOME", data.c_str(), 1);         /* never the user's own library */
    void* so = dlopen(PATH, RTLD_NOW | RTLD_LOCAL);
    auto* entry = so ? (const clap_plugin_entry_t*)dlsym(so, "clap_entry") : nullptr;
    if (!entry || !entry->init(PATH) || !(factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID))) {
        std::printf("FAIL: cannot load %s (%s)\n", PATH, dlerror());
        return 1;
    }
    struct { const char* name; void (*fn)(); } t[] = {
        {"test_plays_a_note", test_plays_a_note},
        {"test_fresh_transport_silent", test_fresh_transport_silent},
        {"test_two_instances_apart", test_two_instances_apart},
        {"test_state_roundtrip", test_state_roundtrip},
        {"test_state_rejects_garbage", test_state_rejects_garbage},
        {"test_library_written", test_library_written},
        {"test_autosave_stays_private", test_autosave_stays_private},
        {"test_missing_core_keeps_state", test_missing_core_keeps_state},
        {"test_no_temp_leftovers", test_no_temp_leftovers},
        {"test_param_list", test_param_list},
        {"test_global_param_changes_state", test_global_param_changes_state},
        {"test_gui_extension", test_gui_extension},
        {"test_layer_plays_any_channel", test_layer_plays_any_channel},
        {"test_saved_firmware_edits_preserved_on_restore", test_saved_firmware_edits_preserved_on_restore},
        {"test_disable_slot_silences_held_notes", test_disable_slot_silences_held_notes},
        {"test_overlapping_note_ownership_clap", test_overlapping_note_ownership_clap},
        {"test_truncation_preserves_note_off", test_truncation_preserves_note_off},
    };
    for (auto& x : t) {
        int f = fails;
        x.fn();
        std::printf("%s %s\n", fails == f ? "ok  " : "FAIL", x.name);
        std::fflush(stdout);
    }
    entry->deinit();
    std::printf(fails ? "FAIL: %d\n" : "PASS: all\n", fails);
    if (!fails)
        std::system(("rm -rf " + data).c_str());
    return fails != 0;
}
