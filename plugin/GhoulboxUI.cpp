/* SPDX-License-Identifier: GPL-3.0-only */
/* GHOULBOX's window: the live screen, every hardware button and encoder, and the panel around them. It draws from the
 * view its instance publishes (host/ui_bridge) and sends the hardware's inputs back the same way; the panel's knobs
 * and faders are the plugin's parameters. It never calls the core. Layout, keys, pixels and knob arithmetic: ui/. */
#include "DistrhoUI.hpp"
#include "fonts.hpp"
#include "global_params.h"
#include "keymap.h"
#include "knob_math.h"
#include "layout.h"
#include "pixels.h"
#include "ui_bridge.h"
#include "ui_link.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {
const Color NIGHT(8, 8, 8), BONE(0xef, 0xdf, 0xc6), DIM(0xb3, 0xa5, 0x8c), AMBER(0xef, 0x9a, 0x29),
    EMBER(0xef, 0x4d, 0x42), FRAME(0x6b, 0x45, 0x13), STONE(0x3a, 0x3a, 0x3e), PLAQUE(0x14, 0x12, 0x10),
    GREEN(0x5f, 0xe0, 0x8a);
const char* const BTN_NAME[GB_NBTN] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                       "PLAY", "REC", "OCT-", "OCT+"};
const char* const ENC_NAME[GB_NENC] = {"SELECT", "ALGO", "PRESETS", "K1", "K2", "K3", "K4"};
constexpr float kEncStepPx = 12;                         // a vertical drag of 12 px: one encoder step
constexpr uint32_t kPlayGreen = 1u << 14;                // web_lit_buttons: PLAY's green
/* the plugin's parameter ids (plugin/GhoulboxPlugin.cpp: append-only) */
constexpr uint32_t kMaster = 0, kHostSync = 1, kTrackFirst = 2, kTrackParams = 8, kGlobalFirst = 34, kParams = 40;
constexpr uint32_t trackParam(uint32_t t, uint32_t k) { return kTrackFirst + t * kTrackParams + k; }   // k: 0 Level, 4.. Knob
bool isPill(int g) { return g == GB_G_EVOL || g == GB_G_RTYPE; }
}

class GhoulboxUI : public UI {
public:
    GhoulboxUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT),
          bridge_(ghoulboxBridge(getPluginInstancePointer())),
          view_(new gb_view()),
          screen_(UiBridge::kScreen),
          rgba_(UiBridge::kScreen * 4)
    {
        setGeometryConstraints((uint)(kW * 0.6f), (uint)(kH * 0.6f), true);
        text_ = createFontFromMemory("vt323", fonts::VT323_RegularData, fonts::VT323_RegularDataSize, false);
        mark_ = createFontFromMemory("pirata", fonts::PirataOne_RegularData, fonts::PirataOne_RegularDataSize, false);
        if (bridge_)
            bridge_->setOpen(true);
    }

    ~GhoulboxUI() override
    {
        if (!bridge_)
            return;
        releaseAll();                                   // (nothing stays held on the device once the window goes)
        bridge_->setOpen(false);
    }

protected:
    void parameterChanged(uint32_t i, float v) override
    {
        if (i < param_.size())
            param_[i] = v;
        repaint();
    }

    void uiIdle() override
    {
        if (!bridge_ || !bridge_->read(*view_, screen_.data()))
            return;
        screenToRgba(screen_.data(), rgba_.data());
        haveView_ = imageDirty_ = true;                 // (uploaded in onNanoDisplay: GL is current only there)
        repaint();
    }

    void onNanoDisplay() override
    {
        beginPath();
        rect(0, 0, getWidth(), getHeight());
        fillColor(NIGHT);
        fill();
        if (!bridge_) {
            say(getWidth() / 2.f, getHeight() / 2.f, "GHOULBOX core missing: reinstall the plugin", 24, BONE,
                ALIGN_CENTER | ALIGN_MIDDLE);
            return;
        }
        if (imageDirty_) {
            if (!screenImg_.isValid())
                screenImg_ = createImageFromRGBA(240, 240, rgba_.data(), 1 << 5);   // NVG_IMAGE_NEAREST: crisp pixels
            else
                screenImg_.update(rgba_.data());
            imageDirty_ = false;
        }
        save();
        scale(s(), s());
        drawPanels();
        drawTopBar();
        drawSounds();
        drawScreen();
        drawButtons();
        drawKnobs();
        drawRight();
        drawEncoders();
        drawTracks();
        drawSteps();
        restore();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (!bridge_)
            return false;
        if (!ev.press) {
            if (pressed_ >= 0)
                push(UiInput::Button, pressed_, 0);
            pressed_ = -1;
            if (dragParam_ >= 0)
                editParameter((uint32_t)dragParam_, false);
            dragParam_ = -1;
            drag_ = Hit{Ctl::None, 0};
            repaint();
            return true;
        }
        const Hit h = hitTest((float)ev.pos.getX(), (float)ev.pos.getY(), s());
        switch (h.ctl) {
        case Ctl::Screen:
            getWindow().focus();
            screenFocus_ = true;
            break;
        case Ctl::Button:
            if (ev.button == kMouseButtonRight || latched_[h.idx])
                toggleLatch(h.idx);
            else if (ev.button == kMouseButtonLeft) {
                push(UiInput::Button, h.idx, 1);
                pressed_ = h.idx;
            }
            break;
        case Ctl::Enc:
            if (ev.button != kMouseButtonLeft)
                return false;
            drag_ = h;
            dragY_ = (float)ev.pos.getY() / s();
            dragAcc_ = 0;
            break;
        case Ctl::SoundRow:
            if (top_ + h.idx < view_->nsounds)
                push(UiInput::Sound, top_ + h.idx, 0);
            break;
        case Ctl::TrackSel:
            push(UiInput::Track, h.idx, 0);
            break;
        case Ctl::Global:
            if (isPill(h.idx)) {
                const GlobalParam& g = GLOBAL_PARAMS[h.idx];
                setParam(kGlobalFirst + h.idx, (float)cycleValue(view_->global[h.idx].value, g.min, g.max));
                break;
            }
            [[fallthrough]];                            // (DENS DRFT MOOD TAPE: knobs)
        case Ctl::Knob:
        case Ctl::Master:
        case Ctl::Fader: {
            if (ev.button != kMouseButtonLeft)
                return false;
            const ParamCtl pc = paramCtl(h);
            dragParam_ = (int)pc.id;
            dragStart_ = pc.live;
            dragY0_ = (float)ev.pos.getY() / s();
            drag_ = h;
            editParameter(pc.id, true);
            break;
        }
        default:
            return false;
        }
        repaint();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragParam_ >= 0) {
            const ParamCtl pc = paramCtl(drag_);
            const float v = dragValue(dragStart_, (float)ev.pos.getY() / s() - dragY0_, pc.min, pc.max);
            if (v != param_[pc.id]) {
                param_[pc.id] = v;
                setParameterValue(pc.id, v);
                repaint();
            }
            return true;
        }
        if (drag_.ctl != Ctl::Enc)
            return false;
        const float y = (float)ev.pos.getY() / s();
        dragAcc_ += dragY_ - y;                         // (up: clockwise)
        dragY_ = y;
        for (; dragAcc_ >= kEncStepPx; dragAcc_ -= kEncStepPx)
            turn(drag_.idx, 1);
        for (; dragAcc_ <= -kEncStepPx; dragAcc_ += kEncStepPx)
            turn(drag_.idx, -1);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const Hit h = hitTest((float)ev.pos.getX(), (float)ev.pos.getY(), s());
        if (!bridge_ || ev.delta.getY() == 0)
            return false;
        const int up = ev.delta.getY() > 0 ? 1 : -1;
        switch (h.ctl) {
        case Ctl::Enc:
            turn(h.idx, up);
            return true;
        case Ctl::SoundRow:
            top_ = scrollTop(top_, -up, view_->nsounds, kSoundRows);
            repaint();
            return true;
        case Ctl::Global:
        case Ctl::Knob:
        case Ctl::Master:
        case Ctl::Fader: {
            const ParamCtl pc = paramCtl(h);
            setParam(pc.id, wheelValue(pc.live, (float)up, pc.min, pc.max));
            return true;
        }
        default:
            return false;
        }
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!bridge_ || (ev.mod & (kModifierControl | kModifierAlt)))
            return false;                               // (the host's shortcuts)
        KeyAction a = ev.key == kKeyUp      ? arrowAction(Arrow::Up)
                      : ev.key == kKeyDown  ? arrowAction(Arrow::Down)
                      : ev.key == kKeyLeft  ? arrowAction(Arrow::Left)
                      : ev.key == kKeyRight ? arrowAction(Arrow::Right)
                                            : keyAction(ev.key);
        if (a.kind == KeyAction::None)
            return false;
        if (a.kind == KeyAction::Enc) {
            if (ev.press)
                turn(a.idx, a.steps);
            return true;
        }
        uint32_t& held = a.kind == KeyAction::Key ? keys_ : keyButtons_;
        const uint32_t bit = 1u << a.idx;
        if (ev.press == ((held & bit) != 0))
            return true;                                // (a key's repeat, or a release never pressed here)
        held ^= bit;
        if (a.kind == KeyAction::Key)
            push(UiInput::Keys, (int32_t)keys_, 0);
        else
            push(UiInput::Button, a.idx, ev.press ? 1 : 0);
        return true;
    }

private:
    float s() const { return getWidth() / kW; }

    void push(UiInput::Kind k, int32_t a, int32_t b) { bridge_->push(UiInput{k, a, b}); }

    /* a panel control's parameter: its id, its range when the window sets it, and the value it starts from (the
     * firmware's live value: a parameter still unset (-1) shows, and is dragged from, what the device plays) */
    struct ParamCtl { uint32_t id; float min, max, live; };
    ParamCtl paramCtl(Hit h) const
    {
        const uint32_t sel = view_->sel % 4;
        switch (h.ctl) {
        case Ctl::Knob: return {trackParam(sel, 4 + (uint32_t)h.idx), 0, 127, (float)view_->knob[h.idx].pos};
        case Ctl::Fader: return {trackParam((uint32_t)h.idx, 0), 0, 127, (float)view_->track[h.idx].level.pos};
        case Ctl::Global: {
            const GlobalParam& g = GLOBAL_PARAMS[h.idx];
            return {kGlobalFirst + (uint32_t)h.idx, (float)g.min, (float)g.max, (float)view_->global[h.idx].value};
        }
        default: return {kMaster, 0, 1023, param_[kMaster]};
        }
    }

    void setParam(uint32_t id, float v)                 // one step: a pill's click, a wheel notch
    {
        param_[id] = v;
        editParameter(id, true);
        setParameterValue(id, v);
        editParameter(id, false);
        repaint();
    }

    void turn(int enc, int steps)
    {
        push(UiInput::Enc, enc, steps);
        encAngle_[enc] += steps * 0.3f;
        repaint();
    }

    void toggleLatch(int b)
    {
        latched_[b] = !latched_[b];
        push(UiInput::Button, b, latched_[b] ? 1 : 0);
    }

    void releaseAll()
    {
        for (int b = 0; b < GB_NBTN; b++)
            if (latched_[b] || pressed_ == b || (keyButtons_ >> b & 1u))
                push(UiInput::Button, b, 0);
        if (keys_)
            push(UiInput::Keys, 0, 0);
    }

    void say(float x, float y, const char* str, float size, const Color& c, int align, FontId font = -1)
    {
        fontFaceId(font >= 0 ? font : text_);
        fontSize(size);
        fillColor(c);
        textAlign(align);
        text(x, y, str, nullptr);
    }

    void box(const Rect& r, const Color& fillC, const Color& strokeC, float width = 1, float radius = 3)
    {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, radius);
        fillColor(fillC);
        fill();
        strokeColor(strokeC);
        strokeWidth(width);
        stroke();
    }

    void drawScreen()
    {
        const Rect r = rectOf(Hit{Ctl::Screen, 0});
        box(Rect{r.x - 6, r.y - 6, r.w + 12, r.h + 12}, FRAME, screenFocus_ ? AMBER : FRAME, 1, 2);
        beginPath();
        rect(r.x, r.y, r.w, r.h);
        if (haveView_) {
            fillPaint(imagePattern(r.x, r.y, r.w, r.h, 0, screenImg_, 1));
            fill();
        } else {
            fillColor(Color(0, 0, 0));
            fill();
            say(r.x + r.w / 2, r.y + r.h / 2, "WAITING FOR AUDIO", 22, DIM, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void drawButtons()
    {
        for (auto& c : controls()) {
            if (c.first.ctl != Ctl::Button)
                continue;
            const int b = c.first.idx;
            const bool green = b == GB_BTN_PLAY && (view_->lit & kPlayGreen);
            const bool lit = green || (view_->lit >> b & 1u), glow = view_->dim >> b & 1u;
            const bool held = latched_[b] || pressed_ == b || (keyButtons_ >> b & 1u);
            box(c.second, green ? GREEN : lit ? AMBER : held ? STONE : PLAQUE,
                latched_[b] ? EMBER : glow ? AMBER.withAlpha(0.5f) : STONE, latched_[b] ? 2 : 1);
            say(c.second.x + c.second.w / 2, c.second.y + c.second.h / 2 + 1, BTN_NAME[b], c.second.h > 30 ? 22 : 18,
                lit ? NIGHT : BONE, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void heading(float x, float y, const char* str) { say(x, y, str, 18, AMBER, ALIGN_LEFT | ALIGN_TOP); }

    void drawPanels()
    {
        for (const Rect& r : {Rect{6, 52, 252, 652}, Rect{270, 52, 500, 652}, Rect{782, 52, 252, 652},
                              Rect{6, 708, 398, 146}, Rect{408, 708, 626, 146}})
            box(r, PLAQUE, FRAME, 1, 2);
    }

    void drawTopBar()
    {
        char b[96];
        say(16, 26, "GHOULBOX", 36, AMBER, ALIGN_LEFT | ALIGN_MIDDLE, mark_);
        std::snprintf(b, sizeof b, "%s  ·  %d BPM  ·  %s", view_->playing ? "PLAYING" : "STOPPED", (int)view_->bpm,
                      param_[kHostSync] > 0.5f ? "SYNCED" : "INTERNAL");
        say(kW - 16, 26, b, 22, view_->playing ? BONE : DIM, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawSounds()
    {
        char b[48];
        if (view_->sound_now != lastSoundNow_) {        // (a new sound: the list shows it; the wheel scrolls freely)
            top_ = listTop(view_->sound_now, view_->nsounds, kSoundRows, top_);
            lastSoundNow_ = view_->sound_now;
        }
        std::snprintf(b, sizeof b, "SOUNDS · TRACK %d", view_->sel + 1);
        heading(16, 56, b);
        const Rect list = rectOf(Hit{Ctl::SoundList, 0});
        for (int i = 0; i < kSoundRows && top_ + i < view_->nsounds; i++) {
            const Rect r{list.x, list.y + i * kRowH, list.w, kRowH};
            const bool cur = top_ + i == view_->sound_now;
            if (cur)
                box(Rect{r.x + 2, r.y + 1, r.w - 4, r.h - 2}, FRAME, FRAME, 1, 2);
            say(r.x + 10, r.y + r.h / 2 + 1, view_->sound[top_ + i], 20, cur ? BONE : DIM, ALIGN_LEFT | ALIGN_MIDDLE);
        }
    }

    /* a parameter knob: an arc over 270 degrees at `pos` (0..1), its label and value under it */
    void knob(const Rect& r, float pos, const char* label, const char* value, bool active)
    {
        const float cx = r.x + r.w / 2, cy = r.y + 17, rad = 15, a0 = 0.75f * 3.14159f, span = 1.5f * 3.14159f;
        beginPath();
        arc(cx, cy, rad, a0, a0 + span, NanoVG::CW);
        strokeColor(STONE);
        strokeWidth(4);
        stroke();
        beginPath();
        arc(cx, cy, rad, a0, a0 + span * std::fmax(0.f, std::fmin(1.f, pos)) + 0.001f, NanoVG::CW);
        strokeColor(active ? BONE : AMBER);
        stroke();
        say(cx, r.y + 34, label, 14, DIM, ALIGN_CENTER | ALIGN_TOP);
        say(cx, r.y + 45, value, 14, BONE, ALIGN_CENTER | ALIGN_TOP);
    }

    void pill(const Rect& r, const char* str, bool on)
    {
        box(r, on ? AMBER : PLAQUE, on ? AMBER : STONE, 1, 4);
        say(r.x + r.w / 2, r.y + r.h / 2 + 1, str, 18, on ? NIGHT : BONE, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawKnobs()
    {
        char b[48];
        std::snprintf(b, sizeof b, "TRACK %d · %s", view_->sel + 1, view_->track[view_->sel % 4].sound);
        heading(280, 624, b);
        for (int k = 0; k < 4; k++) {
            const gb_ctl& c = view_->knob[k];
            knob(rectOf(Hit{Ctl::Knob, k}), c.pos / 127.f, c.label, c.text, drag_ == Hit{Ctl::Knob, k});
        }
    }

    void drawRight()
    {
        char b[32];
        heading(800, 56, "DREAM");
        heading(800, 200, "FX");
        heading(800, 284, "MASTER");
        for (int g = 0; g < GB_NG; g++) {
            const gb_ctl& c = view_->global[g];
            const Rect r = rectOf(Hit{Ctl::Global, g});
            if (isPill(g)) {
                std::snprintf(b, sizeof b, "%s %s", c.label, c.text);
                pill(r, b, g == GB_G_EVOL && c.value);
            } else
                knob(r, c.max > c.min ? (float)(c.value - c.min) / (c.max - c.min) : 0, c.label, c.text,
                     drag_ == Hit{Ctl::Global, g});
        }
        std::snprintf(b, sizeof b, "%d", (int)std::lround(param_[kMaster]));
        knob(rectOf(Hit{Ctl::Master, 0}), param_[kMaster] / 1023.f, "VOL", b, drag_ == Hit{Ctl::Master, 0});
    }

    void drawTracks()
    {
        char b[16];
        heading(16, 712, "TRACKS");
        for (int t = 0; t < 4; t++) {
            const Rect sel = rectOf(Hit{Ctl::TrackSel, t}), fad = rectOf(Hit{Ctl::Fader, t});
            std::snprintf(b, sizeof b, "T%d", t + 1);
            pill(sel, b, t == view_->sel);
            std::snprintf(b, sizeof b, "%.9s", view_->track[t].sound);
            say(sel.x + sel.w / 2, 766, b, 16, DIM, ALIGN_CENTER | ALIGN_MIDDLE);
            const bool mute = view_->track[t].mute;
            box(Rect{fad.x + 7, fad.y, 6, fad.h}, NIGHT, STONE, 1, 3);
            const float y = fad.y + (1 - view_->track[t].level.pos / 127.f) * (fad.h - 10);
            box(Rect{fad.x, y, fad.w, 10}, mute ? STONE : drag_ == Hit{Ctl::Fader, t} ? AMBER : BONE, NIGHT, 1, 2);
        }
    }

    void drawSteps()
    {
        char b[48];
        const int first = view_->step_page * 16 + 1;
        std::snprintf(b, sizeof b, "STEPS · TRACK %d · %d-%d", view_->sel + 1, first, first + 15);
        heading(420, 712, b);
        for (int i = 0; i < 16; i++) {
            const Rect r = rectOf(Hit{Ctl::Step, i});
            const bool on = view_->step[i].on, tie = view_->step[i].tie;
            box(r, NIGHT, STONE, 1, 2);
            if (on)
                box(tie ? Rect{r.x + 3, r.y + r.h / 2, r.w - 6, r.h / 2 - 3} : Rect{r.x + 3, r.y + 3, r.w - 6, r.h - 6},
                    AMBER, AMBER, 1, 2);
            if (i == view_->step_now)
                box(Rect{r.x, r.y + r.h + 4, r.w, 4}, EMBER, EMBER, 1, 1);
        }
    }

    void dial(const Rect& r, float angle, const Color& pointer)
    {
        const float cx = r.x + r.w / 2, cy = r.y + r.w / 2, rad = r.w / 2 - 3;
        beginPath();
        circle(cx, cy, rad);
        fillColor(PLAQUE);
        fill();
        strokeColor(FRAME);
        strokeWidth(2);
        stroke();
        beginPath();
        moveTo(cx + std::sin(angle) * rad * 0.35f, cy - std::cos(angle) * rad * 0.35f);
        lineTo(cx + std::sin(angle) * rad * 0.9f, cy - std::cos(angle) * rad * 0.9f);
        strokeColor(pointer);
        strokeWidth(2.5f);
        stroke();
    }

    void drawEncoders()
    {
        const Rect first = rectOf(Hit{Ctl::Enc, GB_ENC_SELECT});
        say(800, first.y - 20, "HARDWARE", 18, AMBER, ALIGN_LEFT | ALIGN_TOP);
        for (int e = 0; e < GB_NENC; e++) {
            const Rect r = rectOf(Hit{Ctl::Enc, e});
            dial(r, encAngle_[e], drag_ == Hit{Ctl::Enc, e} ? AMBER : BONE);
            say(r.x + r.w / 2, r.y + r.h, ENC_NAME[e], 13, DIM, ALIGN_CENTER | ALIGN_TOP);
        }
    }

    UiBridge* const bridge_;
    std::unique_ptr<gb_view> view_;
    std::vector<uint16_t> screen_;
    std::vector<uint8_t> rgba_;
    std::vector<float> param_ = std::vector<float>(kParams, -1.f);   // the plugin's values (parameterChanged)
    FontId text_ = -1, mark_ = -1;
    NanoImage screenImg_;
    bool haveView_ = false, imageDirty_ = false, screenFocus_ = false;
    bool latched_[GB_NBTN] = {};
    int pressed_ = -1;                                   // the button the left mouse holds
    uint32_t keys_ = 0, keyButtons_ = 0;                 // held by the computer keyboard
    Hit drag_{Ctl::None, 0};
    float dragY_ = 0, dragAcc_ = 0;
    float encAngle_[GB_NENC] = {};
    int dragParam_ = -1;                                 // the parameter a drag sets, -1 = none
    float dragStart_ = 0, dragY0_ = 0;
    int top_ = 0;                                        // the sound list's first shown row
    uint32_t lastSoundNow_ = 0xFFFF;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhoulboxUI)
};

UI* createUI() { return new GhoulboxUI(); }

END_NAMESPACE_DISTRHO
