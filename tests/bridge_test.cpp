/* SPDX-License-Identifier: GPL-3.0-only */
/* Plan 2, Task 2: the UI bridge (inputs in, view and screen out, no waiting on the audio side). */
#include "../host/ui_bridge.h"
#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; return; } } while (0)

static void test_order_kept()
{
    auto br = std::make_unique<UiBridge>();
    const int N = 100000;
    std::thread prod([&] {
        for (int i = 0; i < N; i++)
            while (!br->push(UiInput{UiInput::Enc, GB_ENC_K1, i}))
                std::this_thread::yield();
    });
    std::vector<int> got;
    got.reserve(N);
    while ((int)got.size() < N)
        br->drain([&](const UiInput& in) { got.push_back(in.b); });
    prod.join();
    for (int i = 0; i < N; i++)
        CHECK(got[i] == i);
}

static void test_press_release_split()
{
    auto br = std::make_unique<UiBridge>();
    br->push(UiInput{UiInput::Button, GB_BTN_SEQ, 1});
    br->push(UiInput{UiInput::Button, GB_BTN_SEQ, 0});
    br->push(UiInput{UiInput::Enc, GB_ENC_K1, 1});
    std::vector<UiInput> a, b;
    br->drain([&](const UiInput& in) { a.push_back(in); });
    br->drain([&](const UiInput& in) { b.push_back(in); });
    CHECK(a.size() == 1 && a[0].kind == UiInput::Button && a[0].b == 1);
    CHECK(b.size() == 2 && b[0].kind == UiInput::Button && b[0].b == 0 && b[1].kind == UiInput::Enc);
}

static void test_publish_not_torn()
{
    auto br = std::make_unique<UiBridge>();
    std::atomic<bool> done{false};
    std::thread pub([&] {
        auto v = std::make_unique<gb_view>();
        std::vector<uint16_t> s(UiBridge::kScreen);
        for (uint32_t k = 1; k <= 10000; k++) {
            std::fill(s.begin(), s.end(), (uint16_t)k);
            v->draws = k;
            br->publish(*v, s.data());
        }
        done = true;
    });
    auto v = std::make_unique<gb_view>();
    std::vector<uint16_t> s(UiBridge::kScreen);
    int reads = 0, torn = 0;
    while (!done || br->read(*v, s.data())) {
        if (!br->read(*v, s.data()))
            continue;
        reads++;
        for (uint16_t p : s)
            torn += p != (uint16_t)v->draws;
    }
    pub.join();
    CHECK(reads > 0);
    CHECK(torn == 0);
}

static void test_read_before_publish()
{
    auto br = std::make_unique<UiBridge>();
    auto v = std::make_unique<gb_view>();
    v->draws = 77;
    std::vector<uint16_t> s(UiBridge::kScreen, 5);
    CHECK(!br->read(*v, s.data()));
    CHECK(v->draws == 77 && s[0] == 5 && s[UiBridge::kScreen - 1] == 5);
}

static void test_due()
{
    auto br = std::make_unique<UiBridge>();
    auto v = std::make_unique<gb_view>();
    std::vector<uint16_t> s(UiBridge::kScreen);
    for (int k = 0; k < 8; k++)
        CHECK(!br->due(512, 48000));                 // closed: never
    br->setOpen(true);
    int dues = 0;
    for (int k = 0; k < 20; k++)                      // 800 frames = 1/60 s at 48 kHz: about every second block
        if (br->due(512, 48000)) {
            dues++;
            br->publish(*v, s.data());
        }
    CHECK(dues >= 9 && dues <= 11);
    while (!br->due(512, 48000))
        ;
    CHECK(br->due(512, 48000));                       // not published: still due
}

int main()
{
    struct { const char* name; void (*fn)(); } t[] = {
        {"test_order_kept", test_order_kept},
        {"test_press_release_split", test_press_release_split},
        {"test_publish_not_torn", test_publish_not_torn},
        {"test_read_before_publish", test_read_before_publish},
        {"test_due", test_due},
    };
    for (auto& x : t) {
        int f = fails;
        x.fn();
        std::printf("%s %s\n", fails == f ? "ok  " : "FAIL", x.name);
    }
    std::printf(fails ? "FAIL: %d\n" : "PASS: all\n", fails);
    return fails != 0;
}
