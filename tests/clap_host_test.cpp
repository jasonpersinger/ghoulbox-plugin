/* SPDX-License-Identifier: GPL-3.0-only */
/* Task 7: the real plugin (build/plugin/ghoulbox.clap) in a minimal CLAP host: it plays, instances stay apart, its state
 * comes back and garbage is refused, the shared library is written, nothing is left in the temp folder. */
#include "clap/entry.h"
#include "clap/ext/state.h"
#include "clap/host.h"
#include "clap/plugin-factory.h"
#include <chrono>
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
    Inst()
    {
        p = factory->create_plugin(factory, &host, "quest.messy.ghoulbox");
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
    float run(uint32_t n, const uint8_t* midi = nullptr)
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
        clap_process_t pr{};
        pr.steady_time = (int64_t)t;
        pr.frames_count = n;
        pr.audio_outputs = &out;
        pr.audio_outputs_count = 1;
        pr.in_events = &in.list;
        pr.out_events = &outEvents;
        p->process(p, &pr);
        t += n;
        float pk = 0;
        for (uint32_t i = 0; i < n; i++)
            pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
        return pk;
    }
    float runFor(double s, const uint8_t* midi = nullptr)
    {
        float pk = run(512, midi);
        for (uint32_t k = 512; k < s * RATE; k += 4096)
            pk = std::max(pk, run(4096));
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
        {"test_two_instances_apart", test_two_instances_apart},
        {"test_state_roundtrip", test_state_roundtrip},
        {"test_state_rejects_garbage", test_state_rejects_garbage},
        {"test_library_written", test_library_written},
        {"test_no_temp_leftovers", test_no_temp_leftovers},
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
