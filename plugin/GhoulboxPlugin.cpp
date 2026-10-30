/* SPDX-License-Identifier: GPL-3.0-only */
/* GHOULBOX as a plugin instrument, headless: a private firmware core per instance (host/core_loader), the host's MIDI
 * and transport into it on their frames (host/engine, host/clock_gen), its state in the host's project, its flash the
 * SAVE library every instance shares (host/library). Every core call is on the audio thread, or under coreMutex_. */
#include "DistrhoPlugin.hpp"
#include "DistrhoPluginUtils.hpp"
#include "extra/Base64.hpp"
#include "clock_gen.h"
#include "engine.h"
#include "global_params.h"
#include "library.h"
#include "midi_layer.h"
#include "ui_bridge.h"
#include "ui_link.h"
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

START_NAMESPACE_DISTRHO

enum { kMaster, kHostSync, kTrackFirst, kTrackParams = 8, kGlobalFirst = kTrackFirst + 4 * kTrackParams,
       kLayerFirst = kGlobalFirst + 6, kParamCount = kLayerFirst + 4 };   // ids are append-only: saved projects find their values by index
static const struct { const char* name; const char* sym; uint8_t cc; } TRACK_PARAMS[kTrackParams] = {
    {"Level", "level", 7}, {"Pan", "pan", 10}, {"Reverb", "reverb", 91}, {"Delay", "delay", 94},
    {"Knob 1", "knob1", 21}, {"Knob 2", "knob2", 22}, {"Knob 3", "knob3", 23}, {"Knob 4", "knob4", 24},
};
static constexpr uint32_t MAX_EVENTS = 1024;

class GhoulboxPlugin : public Plugin {
public:
    GhoulboxPlugin() : Plugin(kParamCount, 0, 1), library_(Library::defaultPath())
    {
        for (uint32_t i = 0; i < kParamCount; i++)
            param_[i] = i == kMaster ? 800.f : i == kHostSync ? 1.f : i >= kLayerFirst ? 0.f : -1.f;
        dirty_ = (1ull << kMaster) | (1ull << kHostSync);
        events_.reserve(MAX_EVENTS);
        hostEvents_.reserve(MAX_EVENTS);
        layered_.reserve(MAX_EVENTS);
        clockEvents_.reserve(MAX_EVENTS);
        if (isDummyInstance())
            return;
        std::string bin = getBinaryFilename(), err;
        auto core = CoreInstance::open(bin.substr(0, bin.rfind('/')) + "/libghoulbox_core.so", &err);
        if (!core) {
            d_stderr2("GHOULBOX: %s", err.c_str());
            return;
        }
        const GbApi& api = core->api();
        base_ = library_.load(api.nor_size());
        api.nor_private(&privOff_, &privLen_);
        std::vector<uint8_t> img = base_;              // the library, without anyone's autosave: this instance's
        std::fill(img.begin() + privOff_, img.begin() + privOff_ + privLen_, 0xFF);   // music is its own
        api.nor_load(img.data(), (uint32_t)img.size());
        api.boot();
        snap_.resize(base_.size());
        engine_.reset(new Engine(std::move(core), getSampleRate()));
        bridge_.reset(new UiBridge());
        worker_ = std::thread([this] { work(); });
    }

    ~GhoulboxPlugin() override
    {
        if (!engine_)
            return;
        {
            std::lock_guard<std::mutex> g(snapMutex_);
            stop_ = true;
        }
        wake_.notify_one();
        worker_.join();
        snapshot();                                     // (the audio has stopped: whatever the flash holds now)
        flush();
    }

protected:
    const char* getLabel() const override { return "GHOULBOX"; }
    const char* getDescription() const override { return "GHOULBOX, the dungeon-synth firmware for the M-Vave FM-1, in your DAW."; }
    const char* getMaker() const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getHomePage() const override { return "https://ghoulbox.messy.quest"; }
    const char* getLicense() const override { return "GPL-3.0-only"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('G', 'h', 'B', 'x'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable | kParameterIsInteger;
        if (i == kMaster) {
            p.name = "Master";
            p.symbol = "master";
            p.ranges = ParameterRanges(800, 0, 1023);
        } else if (i == kHostSync) {
            p.hints |= kParameterIsBoolean;
            p.name = "Host sync";
            p.symbol = "host_sync";
            p.ranges = ParameterRanges(1, 0, 1);
        } else if (i >= kLayerFirst) {
            p.hints |= kParameterIsBoolean;
            p.name = String("Track ") + String(i - kLayerFirst + 1) + " MIDI layer";   // (plays every MIDI note too)
            p.symbol = String("t") + String(i - kLayerFirst + 1) + "_layer";
            p.ranges = ParameterRanges(0, 0, 1);
        } else if (i >= kGlobalFirst) {
            const GlobalParam& g = GLOBAL_PARAMS[i - kGlobalFirst];
            const uint32_t n = g.names ? (uint32_t)(g.max - g.min + 1) : 0;
            p.name = g.name;
            p.symbol = g.sym;
            p.ranges = ParameterRanges(-1, -1, g.max);
            p.enumValues.count = 1 + n;                 // -1: unset, as the track params; then the firmware's names
            p.enumValues.restrictedMode = false;
            p.enumValues.values = new ParameterEnumerationValue[1 + n];
            p.enumValues.values[0].value = -1;
            p.enumValues.values[0].label = "—";
            for (uint32_t k = 0; k < n; k++) {
                p.enumValues.values[1 + k].value = (float)(g.min + (int)k);
                p.enumValues.values[1 + k].label = g.names[k];
            }
        } else {
            const uint32_t t = (i - kTrackFirst) / kTrackParams, k = (i - kTrackFirst) % kTrackParams;
            p.name = String("Track ") + String(t + 1) + " " + TRACK_PARAMS[k].name;
            p.symbol = String("t") + String(t + 1) + "_" + TRACK_PARAMS[k].sym;
            p.ranges = ParameterRanges(-1, -1, 127);
            p.enumValues.count = 1;                     // -1: unset, the project's own value stands
            p.enumValues.restrictedMode = false;
            p.enumValues.values = new ParameterEnumerationValue[1];
            p.enumValues.values[0].value = -1;
            p.enumValues.values[0].label = "—";
        }
    }

    float getParameterValue(uint32_t i) const override { return param_[i].load(); }
    void setParameterValue(uint32_t i, float v) override   // any thread: run() applies it
    {
        param_[i] = v;
        dirty_ |= 1ull << i;
    }

    void initState(uint32_t, State& s) override
    {
        s.key = "project";
        s.defaultValue = "";
        s.hints = kStateIsOnlyForDSP | kStateIsBase64Blob;
    }
    String getState(const char* key) const override
    {
        if (std::strcmp(key, "project") != 0)
            return String();
        if (!engine_)
            return heldState_;                          // (no core: the project's music, kept as it came)
        std::vector<uint8_t> b(api().state_size());
        uint32_t n;
        {
            std::lock_guard<std::mutex> g(coreMutex_);
            n = api().state_save(b.data(), (uint32_t)b.size());
        }
        return n ? String::asBase64(b.data(), n) : String();
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "project") != 0 || !*value)
            return;
        if (!engine_) {
            heldState_ = value;                         // never saved over with nothing
            return;
        }
        std::vector<uint8_t> b = d_getChunkFromBase64String(value);
        std::lock_guard<std::mutex> g(coreMutex_);
        if (!b.empty() && api().state_load(b.data(), (uint32_t)b.size()))
            dirty_ |= 1ull << kHostSync;               // the project's own CLOCK must not override Host sync
    }

    void activate() override { sampleRateChanged(getSampleRate()); }
    void sampleRateChanged(double rate) override
    {
        if (!engine_)
            return;
        std::lock_guard<std::mutex> g(coreMutex_);
        engine_->setHostRate(rate);
        clock_.reset();
    }

    void run(const float**, float** out, uint32_t frames, const MidiEvent* midi, uint32_t nmidi) override
    {
        if (!engine_) {
            std::memset(out[0], 0, frames * sizeof(float));
            std::memset(out[1], 0, frames * sizeof(float));
            return;
        }
        std::lock_guard<std::mutex> g(coreMutex_);
        applyParams();
        bridge_->drain([this](const UiInput& in) { applyInput(in); });
        clockEvents_.clear();
        if (param_[kHostSync] > 0.5f)
            clock_.block(hostTime(), frames, getSampleRate(), clockEvents_);
        mergeEvents(midi, nmidi);
        engine_->process(out[0], out[1], frames, events_.data(), (uint32_t)events_.size());
        if (bridge_->due(frames, getSampleRate())) {     // (a window is open: its view, ~60 times a second)
            api().view_read(&view_);
            bridge_->publish(view_, api().screen());
        }
        snapshot();
    }

public:
    UiBridge* bridge() { return bridge_.get(); }        // (nullptr: no core)

private:
    const GbApi& api() const { return engine_->core().api(); }

    void applyParams()
    {
        uint64_t d = dirty_.exchange(0);
        for (uint32_t i = 0; d; i++, d >>= 1) {
            if (!(d & 1))
                continue;
            const float v = param_[i];
            if (i == kMaster)
                api().master((uint32_t)std::lround(v));
            else if (i == kHostSync) {
                api().clock_external(v > 0.5f);
                clock_.reset();
            } else if (i >= kLayerFirst) {
                                                        // (read in run: layerMask)
            } else if (i >= kGlobalFirst) {
                if (v >= 0)
                    api().global_set(GLOBAL_PARAMS[i - kGlobalFirst].gb, (int32_t)std::lround(v));
            } else if (v >= 0)
                api().track_cc((i - kTrackFirst) / kTrackParams, TRACK_PARAMS[(i - kTrackFirst) % kTrackParams].cc,
                               (uint8_t)std::lround(v));
        }
    }

    void applyInput(const UiInput& in)                  // a window's input, into the core
    {
        switch (in.kind) {
        case UiInput::Button: api().button((uint32_t)in.a, in.b); break;
        case UiInput::Enc: api().enc((uint32_t)in.a, in.b); break;
        case UiInput::Keys: api().keys((uint32_t)in.a); break;
        case UiInput::Track: api().track_select((uint32_t)in.a); break;
        case UiInput::Sound: api().sound_load((uint32_t)in.a); break;
        }
    }

    HostTime hostTime() const
    {
        const TimePosition& tp = getTimePosition();
        if (!tp.bbt.valid)                              // (no BBT from the host: frames at 120 BPM)
            return HostTime{tp.playing, tp.frame / getSampleRate() * 2.0, 120.0};
        const auto& b = tp.bbt;
        return HostTime{tp.playing,
                        quartersFromBbt(b.barStartTick, b.beat, b.tick, b.ticksPerBeat, b.beatType, beatsAreQuarters_),
                        b.beatsPerMinute};
    }

    uint32_t layerMask() const                          // the lit "Track N MIDI layer" toggles
    {
        uint32_t m = 0;
        for (uint32_t t = 0; t < 4; t++)
            m |= (param_[kLayerFirst + t] > 0.5f ? 1u : 0u) << t;
        return m;
    }

    /* the host's MIDI (onto the lit tracks: host/midi_layer) and the clock, by frame (the clock first on a tie); no
     * allocation */
    void mergeEvents(const MidiEvent* midi, uint32_t nmidi)
    {
        hostEvents_.clear();
        for (uint32_t m = 0; m < nmidi && hostEvents_.size() < MAX_EVENTS; m++) {
            const MidiEvent& e = midi[m];
            if (e.size == 0 || e.size > 3)
                continue;
            hostEvents_.push_back(MidiEv{e.frame, {e.data[0], e.size > 1 ? e.data[1] : (uint8_t)0,
                                                   e.size > 2 ? e.data[2] : (uint8_t)0}});
        }
        layered_.clear();
        layerEvents(hostEvents_.data(), (uint32_t)hostEvents_.size(), layerMask(), api().midi_base(), layered_,
                    MAX_EVENTS);
        events_.clear();
        size_t c = 0, m = 0;
        while (events_.size() < MAX_EVENTS && (m < layered_.size() || c < clockEvents_.size())) {
            if (m < layered_.size() && (c == clockEvents_.size() || layered_[m].frame < clockEvents_[c].frame))
                events_.push_back(layered_[m++]);
            else
                events_.push_back(clockEvents_[c++]);
        }
    }

    /* the flash changed: a copy for the worker (never file I/O here, never a wait) */
    void snapshot()
    {
        const uint32_t w = api().flash_writes();
        if (w == writes_ || !snapMutex_.try_lock())
            return;                                     // (busy: the next block tries again)
        std::memcpy(snap_.data(), api().nor(), snap_.size());
        snapDirty_ = true;
        writes_ = w;
        snapMutex_.unlock();
    }

    /* the latest snapshot into the shared library */
    void flush()
    {
        std::vector<uint8_t> img;
        {
            std::lock_guard<std::mutex> g(snapMutex_);
            if (!snapDirty_)
                return;
            img = snap_;
            snapDirty_ = false;
        }
        std::copy(img.begin() + privOff_, img.begin() + privOff_ + privLen_, base_.begin() + privOff_);   // (never shared)
        if (library_.commit(base_, img))
            base_ = std::move(img);
        else {
            std::lock_guard<std::mutex> g(snapMutex_);
            snapDirty_ = true;                          // (retried on the next tick)
        }
    }

    void work()
    {
        std::unique_lock<std::mutex> l(snapMutex_);
        while (!stop_) {
            wake_.wait_for(l, std::chrono::seconds(1));
            l.unlock();
            flush();
            l.lock();
        }
    }

    std::unique_ptr<Engine> engine_;
    std::unique_ptr<UiBridge> bridge_;                  // the window's way in (with the core)
    gb_view view_{};                                    // (audio thread) the view being published
    String heldState_;                                  // the project's state, while there is no core to hold it
    mutable std::mutex coreMutex_;
    ClockGen clock_;
    const bool beatsAreQuarters_ = std::strcmp(getPluginFormatName(), "CLAP") == 0;   // (DPF's CLAP BBT: quarters)
    std::vector<MidiEv> events_, clockEvents_, hostEvents_, layered_;
    std::atomic<float> param_[kParamCount];
    std::atomic<uint64_t> dirty_{0};

    Library library_;
    std::vector<uint8_t> base_, snap_;                  // base_: the worker's; snap_: under snapMutex_
    std::mutex snapMutex_;
    std::condition_variable wake_;
    bool snapDirty_ = false, stop_ = false;
    uint32_t writes_ = 0;                               // (0: whatever boot wrote is committed too)
    uint32_t privOff_ = 0, privLen_ = 0;                // gb_nor_private: kept out of the library
    std::thread worker_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhoulboxPlugin)
};

Plugin* createPlugin() { return new GhoulboxPlugin(); }

END_NAMESPACE_DISTRHO

UiBridge* ghoulboxBridge(void* pluginInstance)
{
    return pluginInstance ? static_cast<DISTRHO::GhoulboxPlugin*>(static_cast<DISTRHO::Plugin*>(pluginInstance))->bridge()
                          : nullptr;
}
