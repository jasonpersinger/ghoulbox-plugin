/* SPDX-License-Identifier: GPL-3.0-only */
/* Between a plugin window (the UI thread) and its core (the audio thread): the window's inputs go in through a
 * lock-free queue that run() drains; the device's view and screen come out about 60 times a second while a window is
 * open, published with try_lock so the audio thread never waits. The UI thread never calls the core. */
#pragma once
#include "ghoulbox_core.h"
#include <atomic>
#include <cstdint>
#include <mutex>

struct UiInput {
    enum Kind : uint8_t { Button, Enc, Keys, Track, Sound } kind;
    int32_t a, b;   // Button: a = GB_BTN_*, b = 1 down / 0 up; Enc: a = GB_ENC_*, b = steps; Keys: a = mask;
                    // Track: a = 0..3; Sound: a = list index
};

class UiBridge {
public:
    static constexpr uint32_t kCap = 256;
    static constexpr uint32_t kScreen = 240 * 240;

    bool push(const UiInput& in);   // UI thread (one producer); false = full, dropped

    // audio thread (one consumer): the queued inputs in order, stopping before a Button input for a label already
    // changed in this drain (a press and its release never reach the core in one block)
    template <class F> void drain(F&& apply)
    {
        uint32_t changed = 0, t = tail_.load(std::memory_order_relaxed);
        const uint32_t h = head_.load(std::memory_order_acquire);
        for (; t != h; t++) {
            const UiInput& in = ring_[t % kCap];
            if (in.kind == UiInput::Button) {
                const uint32_t bit = 1u << (in.a & 31);
                if (changed & bit)
                    break;
                changed |= bit;
            }
            apply(in);
        }
        tail_.store(t, std::memory_order_release);
    }

    void setOpen(bool open);                                  // UI thread
    bool due(uint32_t frames, double rate);                   // audio thread: open, and >= rate / 60 frames since the
                                                              // last publish (stays true until a publish lands)
    void publish(const gb_view& v, const uint16_t* screen);   // audio thread: skipped, not waited, while the UI reads
    bool read(gb_view& v, uint16_t* screen);                  // UI thread: true = newer than the last read

private:
    UiInput ring_[kCap];
    std::atomic<uint32_t> head_{0}, tail_{0};
    std::atomic<bool> open_{false};
    double since_ = 0;                                        // (audio thread) frames since the last publish
    std::mutex m_;                                            // view_, screen_, seq_
    gb_view view_{};
    uint16_t screen_[kScreen] = {};
    uint32_t seq_ = 0, readSeq_ = 0;                          // readSeq_: the UI thread's
};
