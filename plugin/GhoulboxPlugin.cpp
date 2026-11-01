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
#include "knob_math.h"
#include "library.h"
#include "midi_layer.h"
#include "pi_mutex.h"
#include "ui_bridge.h"
#include "ui_link.h"
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

START_NAMESPACE_DISTRHO

enum { kMaster = 0, kSlotFirst = 1, kSlotParams = 9, kSlotCCParams = 8,
       kGlobalFirst = kSlotFirst + 4 * kSlotParams, kGlobalCount = 2,
       kParamCount = kGlobalFirst + kGlobalCount };
static const struct { const char* name; const char* sym; uint8_t cc; } SLOT_CC_PARAMS[kSlotCCParams] = {
    {"Level", "level", 7}, {"Pan", "pan", 10}, {"Reverb", "reverb", 91}, {"Delay", "delay", 94},
    {"Knob 1", "knob1", 21}, {"Knob 2", "knob2", 22}, {"Knob 3", "knob3", 23}, {"Knob 4", "knob4", 24},
};
static constexpr uint32_t MAX_EVENTS = 1024;

class GhoulboxPlugin : public Plugin {
public:
    GhoulboxPlugin() : Plugin(kParamCount, 0, 1), library_(Library::defaultPath())
    {
        for (uint32_t i = 0; i < kParamCount; i++) {
            if (i == kMaster)
                param_[i] = 800.f;
            else if (i == kSlotFirst)
                param_[i] = 1.f;
            else if (i == kSlotFirst + 1 * kSlotParams ||
                     i == kSlotFirst + 2 * kSlotParams ||
                     i == kSlotFirst + 3 * kSlotParams)
                param_[i] = 0.f;
            else
                param_[i] = -1.f;
        }
        dirty_ = 1ull << kMaster;
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
        api.clock_external(true);
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
    uint32_t getVersion() const override { return d_version(0, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('G', 'h', 'B', 'x'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable | kParameterIsInteger;
        if (i == kMaster) {
            p.name = "Master";
            p.symbol = "master";
            p.ranges = ParameterRanges(800, 0, 1023);
        } else if (i >= kGlobalFirst) {
            const GlobalParam& g = GLOBAL_PARAMS[i - kGlobalFirst];
            const uint32_t n = g.names ? (uint32_t)(g.max - g.min + 1) : 0;
            p.name = g.name;
            p.symbol = g.sym;
            p.ranges = ParameterRanges(-1, -1, g.max);
            p.enumValues.count = 1 + n;                 // -1: unset, as the slot params; then the firmware's names
            p.enumValues.restrictedMode = false;
            p.enumValues.values = new ParameterEnumerationValue[1 + n];
            p.enumValues.values[0].value = -1;
            p.enumValues.values[0].label = "—";
            for (uint32_t k = 0; k < n; k++) {
                p.enumValues.values[1 + k].value = (float)(g.min + (int)k);
                p.enumValues.values[1 + k].label = g.names[k];
            }
        } else {
            const uint32_t rel = i - kSlotFirst;
            const uint32_t t = rel / kSlotParams;
            const uint32_t k = rel % kSlotParams;
            if (k == 0) {
                p.hints |= kParameterIsBoolean;
                p.name = String("Slot ") + String(t + 1) + " Enabled";
                p.symbol = String("s") + String(t + 1) + "_enabled";
                p.ranges = ParameterRanges(t == 0 ? 1 : 0, 0, 1);
            } else {
                p.name = String("Slot ") + String(t + 1) + " " + SLOT_CC_PARAMS[k - 1].name;
                p.symbol = String("s") + String(t + 1) + "_" + SLOT_CC_PARAMS[k - 1].sym;
                p.ranges = ParameterRanges(-1, -1, 127);
                p.enumValues.count = 1;                 // -1: unset, the project's own value stands
                p.enumValues.restrictedMode = false;
                p.enumValues.values = new ParameterEnumerationValue[1];
                p.enumValues.values[0].value = -1;
                p.enumValues.values[0].label = "—";
            }
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
            std::lock_guard<PiMutex> g(coreMutex_);
            const_cast<GhoulboxPlugin*>(this)->reconcileStaleParams();
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
        std::lock_guard<PiMutex> g(coreMutex_);
        if (!b.empty() && api().state_load(b.data(), (uint32_t)b.size())) {
            api().clock_external(true);
            midiLayer_.reset();
            slotEnabledMask_ = layerMask();
        }
    }

    void activate() override { sampleRateChanged(getSampleRate()); }
    void sampleRateChanged(double rate) override
    {
        if (!engine_)
            return;
        std::lock_guard<PiMutex> g(coreMutex_);
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
        std::lock_guard<PiMutex> g(coreMutex_);
        applyParams();
        bridge_->drain([this](const UiInput& in) { applyInput(in); });
        clockEvents_.clear();
        clock_.block(hostTime(), frames, getSampleRate(), clockEvents_);
        mergeEvents(midi, nmidi);
        engine_->process(out[0], out[1], frames, events_.data(), (uint32_t)events_.size());
        for (const auto& e : events_) {
            if ((e.b[0] & 0xF0u) == 0xB0u) {
                const uint32_t t = e.b[0] & 0x0Fu;
                const uint8_t cc = e.b[1] & 0x7Fu;
                const uint8_t val = e.b[2] & 0x7Fu;
                if (t < 4) {
                    for (uint32_t k = 0; k < kSlotCCParams; k++) {
                        if (SLOT_CC_PARAMS[k].cc == cc) {
                            const uint32_t id = kSlotFirst + t * kSlotParams + 1 + k;
                            if (param_[id] >= 0 && (int)std::lround(param_[id]) != val)
                                param_[id] = -1.f;
                            break;
                        }
                    }
                }
            }
        }
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
            else if (i >= kGlobalFirst) {
                if (v >= 0)
                    api().global_set(GLOBAL_PARAMS[i - kGlobalFirst].gb, (int32_t)std::lround(v));
            } else {
                const uint32_t rel = i - kSlotFirst;
                const uint32_t t = rel / kSlotParams;
                const uint32_t k = rel % kSlotParams;
                if (k == 0) {
                    const bool enabled = v > 0.5f;
                    const bool wasEnabled = (slotEnabledMask_ >> t & 1u) != 0;
                    if (wasEnabled && !enabled) {
                        api().midi((uint8_t)(0xB0u | t), 120, 0); // All Sound Off
                        api().midi((uint8_t)(0xB0u | t), 123, 0); // All Notes Off
                        api().midi((uint8_t)(0xB0u | t), 64, 0);  // Sustain Off
                        midiLayer_.resetSlot(t);
                    }
                    if (enabled)
                        slotEnabledMask_ |= (1u << t);
                    else
                        slotEnabledMask_ &= ~(1u << t);
                } else if (v >= 0) {
                    api().track_cc(t, SLOT_CC_PARAMS[k - 1].cc, (uint8_t)std::lround(v));
                }
            }
        }
    }

    void applyInput(const UiInput& in)                  // a window's input, into the core
    {
        switch (in.kind) {
        case UiInput::Button: api().button((uint32_t)in.a, in.b); break;
        case UiInput::Enc: api().enc((uint32_t)in.a, in.b); break;
        case UiInput::Keys: api().keys((uint32_t)in.a); break;
        case UiInput::Track: api().track_select((uint32_t)in.a); break;
        case UiInput::Sound:
            api().sound_load((uint32_t)in.a);
            for (uint32_t k = 0; k < 4; k++)
                param_[kSlotFirst + (view_.sel % 4) * kSlotParams + 5 + k] = -1.f;
            break;
        }
    }

    void reconcileStaleParams()
    {
        api().view_read(&view_);
        for (uint32_t g = 0; g < kGlobalCount; g++) {
            const GlobalParam& gp = GLOBAL_PARAMS[g];
            if (valueParamStale(param_[kGlobalFirst + g], view_.global[gp.gb].value))
                param_[kGlobalFirst + g] = -1.f;
        }
        for (uint32_t t = 0; t < 4; t++) {
            const gb_ctl& c = view_.track[t].level;
            if (ccParamStale(param_[kSlotFirst + t * kSlotParams + 1], c.pos, c.max - c.min))
                param_[kSlotFirst + t * kSlotParams + 1] = -1.f;
        }
        const uint32_t sel = view_.sel % 4;
        for (uint32_t k = 0; k < 4; k++) {
            const gb_ctl& c = view_.knob[k];
            if (ccParamStale(param_[kSlotFirst + sel * kSlotParams + 5 + k], c.pos, c.max - c.min))
                param_[kSlotFirst + sel * kSlotParams + 5 + k] = -1.f;
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

    uint32_t layerMask() const                          // the enabled slot toggles
    {
        uint32_t m = 0;
        for (uint32_t t = 0; t < 4; t++)
            m |= (param_[kSlotFirst + t * kSlotParams] > 0.5f ? 1u : 0u) << t;
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
        midiLayer_.layer(hostEvents_.data(), (uint32_t)hostEvents_.size(), layerMask(), api().midi_base(), layered_,
                         MAX_EVENTS);
        events_.clear();
        size_t c = 0, m = 0;
        while (events_.size() < MAX_EVENTS && (m < layered_.size() || c < clockEvents_.size())) {
            if (m < layered_.size() && (c == clockEvents_.size() || layered_[m].frame < clockEvents_[c].frame))
                events_.push_back(layered_[m++]);
            else
                events_.push_back(clockEvents_[c++]);
        }
        for (; m < layered_.size(); m++) {
            const MidiEv& e = layered_[m];
            const uint8_t st = e.b[0] & 0xF0u;
            bool isRel = (st == 0x80u) || (st == 0x90u && (e.b[2] & 0x7Fu) == 0) ||
                         (st == 0xB0u && ((e.b[1] == 64 && e.b[2] < 64) || e.b[1] == 120 || e.b[1] == 123));
            if (isRel) {
                for (size_t i = events_.size(); i > 0; i--) {
                    const MidiEv& prev = events_[i - 1];
                    const uint8_t pst = prev.b[0] & 0xF0u;
                    bool prevRel = (pst == 0x80u) || (pst == 0x90u && (prev.b[2] & 0x7Fu) == 0) ||
                                   (pst == 0xB0u && ((prev.b[1] == 64 && prev.b[2] < 64) || prev.b[1] == 120 || prev.b[1] == 123));
                    if (!prevRel) {
                        events_[i - 1] = e;
                        break;
                    }
                }
            }
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
    mutable PiMutex coreMutex_;                         // (GB-008: priority inheritance)
    ClockGen clock_;
    MidiLayer midiLayer_;
    uint32_t slotEnabledMask_ = 1u;
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
