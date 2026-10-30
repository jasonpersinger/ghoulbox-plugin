/* SPDX-License-Identifier: GPL-3.0-only */
/* GHOULBOX's window: the live screen, every hardware button and encoder, and the panel around them. It draws from the
 * view its instance publishes (host/ui_bridge) and sends the hardware's inputs back the same way; the panel's knobs
 * and faders are the plugin's parameters. It never calls the core. Layout, keys, pixels and knob arithmetic: ui/. */
#include "DistrhoUI.hpp"
#include "fonts.hpp"
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
        drawScreen();
        drawButtons();
        drawEncoders();
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
        default:
            return false;
        }
        repaint();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
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
        if (!bridge_ || h.ctl != Ctl::Enc || ev.delta.getY() == 0)
            return false;
        turn(h.idx, ev.delta.getY() > 0 ? 1 : -1);
        return true;
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
    std::vector<float> param_ = std::vector<float>(40, -1.f);
    FontId text_ = -1, mark_ = -1;
    NanoImage screenImg_;
    bool haveView_ = false, imageDirty_ = false, screenFocus_ = false;
    bool latched_[GB_NBTN] = {};
    int pressed_ = -1;                                   // the button the left mouse holds
    uint32_t keys_ = 0, keyButtons_ = 0;                 // held by the computer keyboard
    Hit drag_{Ctl::None, 0};
    float dragY_ = 0, dragAcc_ = 0;
    float encAngle_[GB_NENC] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhoulboxUI)
};

UI* createUI() { return new GhoulboxUI(); }

END_NAMESPACE_DISTRHO
