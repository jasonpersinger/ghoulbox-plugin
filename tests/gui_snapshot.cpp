/* SPDX-License-Identifier: GPL-3.0-only */
/* By hand (make gui-snapshot, needs an X display): the real CLAP's window in a bare X11 host, audio running on its own
 * thread so the window gets its view, then a picture of it (ImageMagick's import) in build/gui-snapshot.png. Optional
 * arguments: MIDI-free "actions" sent through the window's own controls are out of scope; this only looks. */
#include "clap/entry.h"
#include "clap/ext/gui.h"
#include "clap/ext/timer-support.h"
#include "clap/host.h"
#include "clap/plugin-factory.h"
#include <X11/Xlib.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <vector>

static const char* PATH = "build/plugin/ghoulbox.clap";
static const clap_plugin_t* plug;
static std::vector<clap_id> timers;

static bool regTimer(const clap_host_t*, uint32_t, clap_id* id) { *id = (clap_id)timers.size(); timers.push_back(*id); return true; }
static bool unregTimer(const clap_host_t*, clap_id) { return true; }
static const clap_host_timer_support_t hostTimer = {regTimer, unregTimer};
static void guiNop(const clap_host_t*) {}
static std::atomic<uint32_t> wantW{0}, wantH{0};
static bool guiResize(const clap_host_t*, uint32_t w, uint32_t h) { wantW = w; wantH = h; return true; }   // (as hosts do)
static bool guiShow(const clap_host_t*) { return true; }
static void guiClosed(const clap_host_t*, bool) {}
static const clap_host_gui_t hostGui = {guiNop, guiResize, guiShow, guiShow, guiClosed};
static const void* hostExt(const clap_host_t*, const char* id)
{
    if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT))
        return &hostTimer;
    if (!std::strcmp(id, CLAP_EXT_GUI))
        return &hostGui;
    return nullptr;
}
static void hostNop(const clap_host_t*) {}
static const clap_host_t host = {CLAP_VERSION, nullptr, "snapshot", "", "", "1", hostExt, hostNop, hostNop, hostNop};

static uint32_t evSize(const clap_input_events_t*) { return 0; }
static const clap_event_header_t* evGet(const clap_input_events_t*, uint32_t) { return nullptr; }
static bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "build/gui-snapshot.png";
    const double seconds = argc > 2 ? std::atof(argv[2]) : 3;
    void* so = dlopen(PATH, RTLD_NOW | RTLD_LOCAL);
    auto* entry = so ? (const clap_plugin_entry_t*)dlsym(so, "clap_entry") : nullptr;
    if (!entry || !entry->init(PATH))
        return std::printf("cannot load %s\n", PATH), 1;
    auto* f = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    plug = f->create_plugin(f, &host, "quest.messy.ghoulbox");
    if (!plug || !plug->init(plug) || !plug->activate(plug, 48000, 1, 512) || !plug->start_processing(plug))
        return std::printf("cannot start the plugin\n"), 1;

    auto* gui = (const clap_plugin_gui_t*)plug->get_extension(plug, CLAP_EXT_GUI);
    auto* tim = (const clap_plugin_timer_support_t*)plug->get_extension(plug, CLAP_EXT_TIMER_SUPPORT);
    uint32_t ww = 1040, wh = 860;
    if (!gui || !gui->create(plug, CLAP_WINDOW_API_X11, false))
        return std::printf("cannot create the window\n"), 1;
    gui->get_size(plug, &ww, &wh);                       // (the plugin's own size: its default at the screen's scale)
    Display* d = XOpenDisplay(nullptr);
    if (!d)
        return std::printf("no X display\n"), 1;
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, ww, wh, 0, 0, 0);
    XStoreName(d, w, "GHOULBOX snapshot");
    XMapWindow(d, w);
    XFlush(d);

    clap_window_t cw{};
    cw.api = CLAP_WINDOW_API_X11;
    cw.x11 = w;
    if (!gui->set_parent(plug, &cw) || !gui->show(plug))
        return std::printf("cannot open the window\n"), 1;

    {
        uint32_t aw = 0, ah = 0;
        gui->get_size(plug, &aw, &ah);
        std::printf("size: %ux%u before the window, %ux%u after\n", ww, wh, aw, ah);
        if (aw != ww || ah != wh)
            wantW = aw, wantH = ah;
    }
    std::atomic<bool> stop{false};
    std::thread audio([&] {                              // a host's audio thread, in real time
        std::vector<float> l(512), r(512);
        float* ch[2] = {l.data(), r.data()};
        clap_audio_buffer_t ob{};
        ob.data32 = ch;
        ob.channel_count = 2;
        clap_input_events_t in{nullptr, evSize, evGet};
        clap_output_events_t oe{nullptr, evPush};
        int64_t t = 0;
        while (!stop) {
            clap_process_t pr{};
            pr.steady_time = t;
            pr.frames_count = 512;
            pr.audio_outputs = &ob;
            pr.audio_outputs_count = 1;
            pr.in_events = &in;
            pr.out_events = &oe;
            plug->process(plug, &pr);
            t += 512;
            std::this_thread::sleep_for(std::chrono::microseconds(10667));
        }
    });
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {     // the main thread: the plugin's timers and X events
        for (clap_id id : timers)
            if (tim)
                tim->on_timer(plug, id);
        if (wantW) {
            XResizeWindow(d, w, wantW, wantH);
            gui->set_size(plug, wantW, wantH);
            wantW = 0;
        }
        while (XPending(d)) {
            XEvent e;
            XNextEvent(d, &e);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    std::system(("import -window " + std::to_string(w) + " " + out).c_str());
    stop = true;
    audio.join();
    gui->destroy(plug);
    plug->stop_processing(plug);
    plug->deactivate(plug);
    plug->destroy(plug);
    entry->deinit();
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    std::printf("snapshot: %s\n", out);
    return 0;
}
