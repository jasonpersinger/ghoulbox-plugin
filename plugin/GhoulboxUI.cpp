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
#include <chrono>
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
constexpr uint32_t kMaster = 0, kSlotFirst = 1, kSlotParams = 9, kGlobalFirst = kSlotFirst + 4 * kSlotParams,
                   kGlobalCount = 2, kParams = kGlobalFirst + kGlobalCount;
constexpr uint32_t slotParam(uint32_t t, uint32_t k) { return kSlotFirst + t * kSlotParams + k; }
bool isPill(int g) { return g == 0; }
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
        if (i < param_.size()) {
            param_[i] = v;
            touched(i);
        }
        repaint();
    }

    void uiIdle() override
    {
        if (tooltipsEnabled_ && hover_.ctl != Ctl::None && !tooltipVisible_ &&
            dragParam_ < 0 && drag_.ctl == Ctl::None) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - hoverStart_).count();
            if (elapsed >= kTooltipDelayMs) {
                tooltipVisible_ = true;
                tooltipPos_ = mousePos_;
                repaint();
            }
        }
        if (!bridge_ || !bridge_->read(*view_, screen_.data()))
            return;
        screenToRgba(screen_.data(), rgba_.data());
        haveView_ = imageDirty_ = true;                 // (uploaded in onNanoDisplay: GL is current only there)
        releaseStale();
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
        drawCheatSheet();
        drawSlots();
        drawTooltip();
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
            hover_ = hitTest((float)ev.pos.getX(), (float)ev.pos.getY(), s());
            hoverStart_ = std::chrono::steady_clock::now();
            tooltipVisible_ = false;
            repaint();
            return true;
        }
        tooltipVisible_ = false;
        hover_ = Hit{Ctl::None, 0};
        const Hit h = hitTest((float)ev.pos.getX(), (float)ev.pos.getY(), s());
        if (h.ctl == Ctl::TooltipToggle) {
            if (ev.button == kMouseButtonLeft) {
                tooltipsEnabled_ = !tooltipsEnabled_;
                hover_ = h;
                hoverStart_ = std::chrono::steady_clock::now();
                repaint();
                return true;
            }
            return false;
        }
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
        case Ctl::Layer:                                // every MIDI note also plays this slot
            setParam(slotParam((uint32_t)h.idx, 0), layerOn(h.idx) ? 0.f : 1.f);
            break;
        case Ctl::Global:
            if (isPill(h.idx)) {
                const GlobalParam& g = GLOBAL_PARAMS[h.idx];
                setParam(kGlobalFirst + h.idx, (float)cycleValue(view_->global[g.gb].value, g.min, g.max));
                break;
            }
            [[fallthrough]];                            // (TAPE: knob)
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
        const float mx = (float)ev.pos.getX() / s();
        const float my = (float)ev.pos.getY() / s();
        mousePos_ = Point<float>(mx, my);
        if (dragParam_ >= 0) {
            const ParamCtl pc = paramCtl(drag_);
            const float v = dragValue(dragStart_, my - dragY0_, pc.min, pc.max);
            if (v != param_[pc.id]) {
                param_[pc.id] = v;
                touched(pc.id);
                setParameterValue(pc.id, v);
                repaint();
            }
            return true;
        }
        if (drag_.ctl == Ctl::Enc) {
            const float y = my;
            dragAcc_ += dragY_ - y;                         // (up: clockwise)
            dragY_ = y;
            for (; dragAcc_ >= kEncStepPx; dragAcc_ -= kEncStepPx)
                turn(drag_.idx, 1);
            for (; dragAcc_ <= -kEncStepPx; dragAcc_ += kEncStepPx)
                turn(drag_.idx, -1);
            return true;
        }
        const Hit h = hitTest((float)ev.pos.getX(), (float)ev.pos.getY(), s());
        if (!(h == hover_)) {
            hover_ = h;
            hoverStart_ = std::chrono::steady_clock::now();
            if (tooltipVisible_) {
                tooltipVisible_ = false;
                repaint();
            }
        }
        return false;
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
        case Ctl::Knob: return {slotParam(sel, 5 + (uint32_t)h.idx), 0, 127, (float)view_->knob[h.idx].pos};
        case Ctl::Fader: return {slotParam((uint32_t)h.idx, 1), 0, 127, (float)view_->track[h.idx].level.pos};
        case Ctl::Global: {
            const GlobalParam& g = GLOBAL_PARAMS[h.idx];
            return {kGlobalFirst + (uint32_t)h.idx, (float)g.min, (float)g.max, (float)view_->global[g.gb].value};
        }
        default: return {kMaster, 0, 1023, param_[kMaster]};
        }
    }

    void setParam(uint32_t id, float v)                 // one step: a pill's click, a wheel notch
    {
        param_[id] = v;
        touched(id);
        editParameter(id, true);
        setParameterValue(id, v);
        editParameter(id, false);
        repaint();
    }

    bool layerOn(int t) const { return param_[slotParam((uint32_t)t, 0)] > 0.5f; }

    void touched(uint32_t id) { lastSet_[id] = std::chrono::steady_clock::now(); }

    /* Review I2: a parameter set from here (or by the host) that the device has since moved away from (a sound load,
     * a HARDWARE knob, a scene, undo) goes back to unset, so the host's project never brings back a stale value over
     * the music. A grace after its last change lets a fresh value reach the device first. */
    void releaseStale()
    {
        const auto now = std::chrono::steady_clock::now();
        auto release = [&](uint32_t id, bool stale) {
            if (!stale || (int)id == dragParam_ || now - lastSet_[id] < std::chrono::milliseconds(300))
                return;
            param_[id] = -1;
            setParameterValue(id, -1);
        };
        const uint32_t sel = view_->sel % 4;
        for (uint32_t k = 0; k < 4; k++) {
            const gb_ctl& c = view_->knob[k];
            release(slotParam(sel, 5 + k), ccParamStale(param_[slotParam(sel, 5 + k)], c.pos, c.max - c.min));
        }
        for (uint32_t t = 0; t < 4; t++) {
            const gb_ctl& c = view_->track[t].level;
            release(slotParam(t, 1), ccParamStale(param_[slotParam(t, 1)], c.pos, c.max - c.min));
        }
        for (uint32_t g = 0; g < kGlobalCount; g++) {
            const GlobalParam& gp = GLOBAL_PARAMS[g];
            release(kGlobalFirst + g, valueParamStale(param_[kGlobalFirst + g], view_->global[gp.gb].value));
        }
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
        if (bridge_)
            bridge_->requestReleaseAll();
    }

    void say(float x, float y, const char* str, float size, const Color& c, int align, FontId font = -1)
    {
        if (!str || !*str)
            return;
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
            const bool lit = view_->lit >> b & 1u, glow = view_->dim >> b & 1u;
            const bool held = latched_[b] || pressed_ == b || (keyButtons_ >> b & 1u);
            box(c.second, lit ? AMBER : held ? STONE : PLAQUE,
                latched_[b] ? EMBER : glow ? AMBER.withAlpha(0.5f) : STONE, latched_[b] ? 2 : 1);
            say(c.second.x + c.second.w / 2, c.second.y + c.second.h / 2 + 1, BTN_NAME[b], c.second.h > 30 ? 22 : 18,
                lit ? NIGHT : BONE, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void heading(float x, float y, const char* str) { say(x, y, str, 18, AMBER, ALIGN_LEFT | ALIGN_TOP); }

    void drawPanels()
    {
        for (const Rect& r : {Rect{6, 52, 252, 652}, Rect{270, 52, 500, 652}, Rect{782, 52, 252, 652},
                              Rect{6, 708, 1028, 146}})
            box(r, PLAQUE, FRAME, 1, 2);
    }

    void drawTopBar()
    {
        char b[96];
        say(16, 26, "GHOULBOX", 36, AMBER, ALIGN_LEFT | ALIGN_MIDDLE, mark_);
        std::snprintf(b, sizeof b, "%d BPM", (int)view_->bpm);
        say(kW - 16, 26, b, 22, BONE, ALIGN_RIGHT | ALIGN_MIDDLE);

        // Tooltip toggle checkbox
        const Rect tr = rectOf(Hit{Ctl::TooltipToggle, 0});
        const Rect boxRect{tr.x, tr.y + 4.f, 14.f, 14.f};
        const bool hovered = hover_.ctl == Ctl::TooltipToggle;
        box(boxRect, PLAQUE, hovered ? AMBER : STONE, 1.f, 2);
        if (tooltipsEnabled_)
            say(boxRect.x + 7.f, boxRect.y + 8.f, "X", 14, AMBER, ALIGN_CENTER | ALIGN_MIDDLE);
        say(tr.x + 22.f, tr.y + 12.f, "TOOLTIPS", 16, tooltipsEnabled_ ? BONE : DIM, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawSounds()
    {
        char b[48];
        if (view_->sound_now != lastSoundNow_) {        // (a new sound: the list shows it; the wheel scrolls freely)
            top_ = listTop(view_->sound_now, view_->nsounds, kSoundRows, top_);
            lastSoundNow_ = view_->sound_now;
        }
        std::snprintf(b, sizeof b, "SOUNDS · SLOT %d", view_->sel + 1);
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
        std::snprintf(b, sizeof b, "SLOT %d · %s", view_->sel + 1, view_->track[view_->sel % 4].sound);
        heading(280, 624, b);
        for (int k = 0; k < 4; k++) {
            const gb_ctl& c = view_->knob[k];
            knob(rectOf(Hit{Ctl::Knob, k}), c.pos / 127.f, c.label, c.text, drag_ == Hit{Ctl::Knob, k});
        }
    }

    void drawRight()
    {
        char b[32];
        heading(800, 68, "MASTER");
        std::snprintf(b, sizeof b, "%d", (int)std::lround(param_[kMaster]));
        knob(rectOf(Hit{Ctl::Master, 0}), param_[kMaster] / 1023.f, "VOL", b, drag_ == Hit{Ctl::Master, 0});

        heading(800, 178, "GLOBAL FX");
        for (uint32_t g = 0; g < kGlobalCount; g++) {
            const GlobalParam& gp = GLOBAL_PARAMS[g];
            const gb_ctl& c = view_->global[gp.gb];
            const Rect r = rectOf(Hit{Ctl::Global, (int)g});
            if (isPill(g)) {
                std::snprintf(b, sizeof b, "TYPE: %s", c.text);
                pill(r, b, false);
            } else
                knob(r, c.max > c.min ? (float)(c.value - c.min) / (c.max - c.min) : 0, c.label, c.text,
                     drag_ == Hit{Ctl::Global, (int)g});
        }
    }

    void drawSlots()
    {
        char b[32];
        heading(16, 712, "SOUND SLOTS");
        for (int t = 0; t < 4; t++) {
            const Rect sel = rectOf(Hit{Ctl::TrackSel, t}), fad = rectOf(Hit{Ctl::Fader, t});
            const Rect lay = rectOf(Hit{Ctl::Layer, t});
            std::snprintf(b, sizeof b, "SLOT %d", t + 1);
            pill(sel, b, t == view_->sel);

            const bool on = layerOn(t);
            box(lay, on ? AMBER : PLAQUE, on ? AMBER : STONE, 1, 3);
            say(lay.x + lay.w / 2, lay.y + lay.h / 2 + 1, on ? "ENABLED" : "OFF", 16, on ? NIGHT : DIM, ALIGN_CENTER | ALIGN_MIDDLE);

            std::snprintf(b, sizeof b, "%.14s", view_->track[t].sound[0] ? view_->track[t].sound : "—");
            say(sel.x + (lay.x + lay.w - sel.x) / 2, 772, b, 18, t == view_->sel ? BONE : DIM, ALIGN_CENTER | ALIGN_MIDDLE);

            const bool mute = view_->track[t].mute;
            box(Rect{fad.x + 9, fad.y, 6, fad.h}, NIGHT, STONE, 1, 3);
            const float y = fad.y + (1 - view_->track[t].level.pos / 127.f) * (fad.h - 10);
            box(Rect{fad.x, y, fad.w, 10}, mute ? STONE : drag_ == Hit{Ctl::Fader, t} ? AMBER : BONE, NIGHT, 1, 2);
            say(fad.x + fad.w / 2, fad.y + fad.h + 8, "VOL", 12, DIM, ALIGN_CENTER | ALIGN_TOP);
        }
    }

    void dial(const Rect& r, float angle, bool active)
    {
        const float cx = r.x + r.w / 2, cy = r.y + r.h / 2, rad = r.w / 2 - 2;
        beginPath();
        circle(cx, cy, rad);
        fillColor(NIGHT);
        fill();
        strokeColor(active ? AMBER : FRAME);
        strokeWidth(2);
        stroke();

        beginPath();
        circle(cx, cy, rad * 0.70f);
        fillColor(PLAQUE);
        fill();
        strokeColor(active ? AMBER.withAlpha(0.7f) : STONE);
        strokeWidth(1);
        stroke();

        beginPath();
        moveTo(cx + std::sin(angle) * rad * 0.25f, cy - std::cos(angle) * rad * 0.25f);
        lineTo(cx + std::sin(angle) * rad * 0.85f, cy - std::cos(angle) * rad * 0.85f);
        strokeColor(active ? BONE : AMBER);
        strokeWidth(2.5f);
        stroke();
    }

    void drawEncoders()
    {
        heading(800, 286, "HARDWARE ENCODERS");
        for (int e = 0; e < 3; e++) {
            const Rect r = rectOf(Hit{Ctl::Enc, e});
            dial(r, encAngle_[e], drag_ == Hit{Ctl::Enc, e});
            say(r.x + r.w / 2, r.y + r.h + 2, ENC_NAME[e], 14, DIM, ALIGN_CENTER | ALIGN_TOP);
        }
        say(800, 426, "KNOBS 1 - 4", 14, DIM, ALIGN_LEFT | ALIGN_TOP);
        for (int e = 3; e < GB_NENC; e++) {
            const Rect r = rectOf(Hit{Ctl::Enc, e});
            dial(r, encAngle_[e], drag_ == Hit{Ctl::Enc, e});
            say(r.x + r.w / 2, r.y + r.h + 2, ENC_NAME[e], 14, DIM, ALIGN_CENTER | ALIGN_TOP);
        }
    }

    void drawCheatSheet()
    {
        const Rect r{794, 536, 228, 148};
        box(r, NIGHT, FRAME, 1, 3);
        say(r.x + 12, r.y + 10, "QUICK REFERENCE", 15, AMBER, ALIGN_LEFT | ALIGN_TOP);
        say(r.x + 12, r.y + 34, "· RIGHT-CLICK: Latch button", 14, BONE, ALIGN_LEFT | ALIGN_TOP);
        say(r.x + 12, r.y + 56, "· SCROLL / DRAG: Turn dials", 14, BONE, ALIGN_LEFT | ALIGN_TOP);
        say(r.x + 12, r.y + 78, "· Z/A KEYS: Play musical notes", 14, BONE, ALIGN_LEFT | ALIGN_TOP);
        say(r.x + 12, r.y + 100, "· 1-0 KEYS: Trigger buttons", 14, BONE, ALIGN_LEFT | ALIGN_TOP);
        say(r.x + 12, r.y + 122, "· [ / ] KEYS: Octave down/up", 14, BONE, ALIGN_LEFT | ALIGN_TOP);
    }

    const char* tooltipFor(Hit h) const
    {
        switch (h.ctl) {
        case Ctl::Button:
            switch (h.idx) {
            case GB_BTN_FX: return "FX: Reverb & Delay sends, Chorus/Flange, Distortion";
            case GB_BTN_SCL: return "SCL: Scale selection, Root note, and Microtuning";
            case GB_BTN_ENV: return "ENV: Attack, Decay, Sustain, and Release envelope times";
            case GB_BTN_LFO: return "LFO: Low Frequency Oscillator rate, waveform & depth";
            case GB_BTN_EDIT: return "EDIT: FM sound engine, algorithm, and operator parameters";
            case GB_BTN_HOME: return "HOME: Main performance screen (Hold for system Menu)";
            case GB_BTN_SAVE: return "SAVE: Store current sound into shared user library";
            case GB_BTN_ARP: return "ARP: Arpeggiator mode, tempo division, gate & octave";
            case GB_BTN_OCTDN: return "OCT-: Shift keyboard pitch down one octave";
            case GB_BTN_OCTUP: return "OCT+: Shift keyboard pitch up one octave";
            default: return nullptr;
            }
        case Ctl::Knob: {
            static char b[80];
            const gb_ctl& c = view_->knob[h.idx];
            std::snprintf(b, sizeof b, "Slot %d Knob %d: %s (%s) · Drag up/down",
                          view_->sel + 1, h.idx + 1, c.label, c.text);
            return b;
        }
        case Ctl::Global:
            return h.idx == 0 ? "REVERB TYPE: Click to cycle Room, Spring, and Hall"
                              : "TAPE: Analog warmth, saturation & hiss · Drag to adjust";
        case Ctl::Master:
            return "MASTER VOLUME: Overall plugin output level · Drag to adjust";
        case Ctl::Enc:
            switch (h.idx) {
            case GB_ENC_SELECT: return "SELECT: Navigate menu items & settings · Scroll or drag";
            case GB_ENC_ALGO: return "ALGORITHM: Switch algorithms & options · Scroll or drag";
            case GB_ENC_PRESETS: return "PRESETS: Browse preset sounds · Scroll or drag";
            case GB_ENC_K1: return "K1 ENCODER: Hardware encoder 1 · Scroll or drag";
            case GB_ENC_K2: return "K2 ENCODER: Hardware encoder 2 · Scroll or drag";
            case GB_ENC_K3: return "K3 ENCODER: Hardware encoder 3 · Scroll or drag";
            case GB_ENC_K4: return "K4 ENCODER: Hardware encoder 4 · Scroll or drag";
            default: return nullptr;
            }
        case Ctl::TrackSel: {
            static char b[64];
            std::snprintf(b, sizeof b, "SLOT %d: Click to select slot for sound editing", h.idx + 1);
            return b;
        }
        case Ctl::Layer: {
            static char b[80];
            std::snprintf(b, sizeof b, "SLOT %d ON/OFF: Toggle slot. All enabled slots play in unison", h.idx + 1);
            return b;
        }
        case Ctl::Fader: {
            static char b[64];
            std::snprintf(b, sizeof b, "SLOT %d VOLUME: Output level (CC 7) · Drag to adjust", h.idx + 1);
            return b;
        }
        case Ctl::TooltipToggle:
            return "TOOLTIPS: Toggle hover help popups on/off";
        case Ctl::SoundList:
        case Ctl::SoundRow:
            return "SOUND BROWSER: Click to load sound into active slot · Scroll to browse";
        case Ctl::Screen:
            return "FIRMWARE DISPLAY: Live M-Vave FM-1 screen · Click to focus keyboard";
        default:
            return nullptr;
        }
    }

    void drawTooltip()
    {
        if (!tooltipsEnabled_ || !tooltipVisible_ || hover_.ctl == Ctl::None || dragParam_ >= 0 || drag_.ctl != Ctl::None)
            return;
        const char* tip = tooltipFor(hover_);
        if (!tip || !*tip)
            return;

        fontFaceId(text_);
        fontSize(16);
        Rectangle<float> bounds;
        textBounds(0, 0, tip, nullptr, bounds);
        const float tw = bounds.getWidth() + 18.f;
        const float th = 26.f;

        float tx = tooltipPos_.getX() + 14.f;
        float ty = tooltipPos_.getY() + 14.f;
        if (tx + tw > kW - 8.f)
            tx = tooltipPos_.getX() - tw - 8.f;
        if (ty + th > kH - 8.f)
            ty = tooltipPos_.getY() - th - 8.f;
        if (tx < 8.f)
            tx = 8.f;
        if (ty < 8.f)
            ty = 8.f;

        box(Rect{tx, ty, tw, th}, NIGHT, AMBER, 1.5f, 4);
        say(tx + 9.f, ty + th / 2.f + 1.f, tip, 16, BONE, ALIGN_LEFT | ALIGN_MIDDLE);
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
    Hit hover_{Ctl::None, 0};
    bool tooltipsEnabled_ = true;
    bool tooltipVisible_ = false;
    Point<float> mousePos_{0, 0};
    Point<float> tooltipPos_{0, 0};
    std::chrono::steady_clock::time_point hoverStart_{};
    static constexpr int kTooltipDelayMs = 450;
    float dragY_ = 0, dragAcc_ = 0;
    float encAngle_[GB_NENC] = {};
    int dragParam_ = -1;                                 // the parameter a drag sets, -1 = none
    float dragStart_ = 0, dragY0_ = 0;
    int top_ = 0;                                        // the sound list's first shown row
    std::vector<std::chrono::steady_clock::time_point> lastSet_ = std::vector<std::chrono::steady_clock::time_point>(kParams);
    uint32_t lastSoundNow_ = 0xFFFF;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhoulboxUI)
};

UI* createUI() { return new GhoulboxUI(); }

END_NAMESPACE_DISTRHO
