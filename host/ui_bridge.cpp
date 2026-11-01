/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_bridge.h"
#include <cstring>

bool UiBridge::push(const UiInput& in)
{
    const uint32_t h = head_.load(std::memory_order_relaxed);
    if (h - tail_.load(std::memory_order_acquire) >= kCap) {
        if (in.kind == UiInput::Button && in.b == 0) {
            pendingReleaseButtons_.fetch_or(1u << (in.a & 31), std::memory_order_release);
            return true;
        }
        if (in.kind == UiInput::Keys && in.a == 0) {
            pendingReleaseKeys_.store(true, std::memory_order_release);
            return true;
        }
        return false;
    }
    ring_[h % kCap] = in;
    head_.store(h + 1, std::memory_order_release);
    return true;
}

void UiBridge::requestReleaseAll()
{
    releaseAll_.store(true, std::memory_order_release);
}

void UiBridge::setOpen(bool open)
{
    open_.store(open, std::memory_order_relaxed);
    if (!open)
        requestReleaseAll();
}

bool UiBridge::due(uint32_t frames, double rate)
{
    if (!open_.load(std::memory_order_relaxed)) {
        since_ = 0;
        return false;
    }
    since_ += frames;
    return since_ >= rate / 60.0;
}

void UiBridge::publish(const gb_view& v, const uint16_t* screen)
{
    if (!m_.try_lock())
        return;                                   // (the UI is reading: the next block tries again)
    view_ = v;
    std::memcpy(screen_, screen, sizeof screen_);
    seq_++;
    m_.unlock();
    since_ = 0;
}

bool UiBridge::read(gb_view& v, uint16_t* screen)
{
    std::lock_guard<std::mutex> g(m_);
    if (seq_ == readSeq_)
        return false;
    v = view_;
    std::memcpy(screen, screen_, sizeof screen_);
    readSeq_ = seq_;
    return true;
}
