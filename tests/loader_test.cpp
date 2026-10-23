/* SPDX-License-Identifier: GPL-3.0-only */
/* Task 3: each CoreInstance is a private copy of libghoulbox_core.so: its own firmware globals. */
#include "../host/core_loader.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <vector>

static const char* LIB = "build/libghoulbox_core.so";
static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; return; } } while (0)

static std::unique_ptr<CoreInstance> boot()
{
    std::string err;
    auto c = CoreInstance::open(LIB, &err);
    if (!c) {
        std::fprintf(stderr, "  open: %s\n", err.c_str());
        return nullptr;
    }
    c->api().nor_load(nullptr, 0);
    c->api().boot();
    return c;
}
static float peak(CoreInstance& c, uint32_t n)
{
    std::vector<float> l(n), r(n);
    float p = 0;
    c.api().render(l.data(), r.data(), n);
    for (uint32_t i = 0; i < n; i++)
        p = std::max(p, std::max(std::fabs(l[i]), std::fabs(r[i])));
    return p;
}
static std::vector<uint8_t> save(CoreInstance& c)
{
    std::vector<uint8_t> b(c.api().state_size());
    b.resize(c.api().state_save(b.data(), (uint32_t)b.size()));
    return b;
}

static void test_two_instances_independent()
{
    auto a = boot(), b = boot();
    CHECK(a && b);
    a->api().midi(0x90, 60, 100);
    CHECK(peak(*a, 8820) > 0.01f);
    CHECK(peak(*b, 8820) < 1e-4f);
}

static void test_state_isolated()
{
    auto a = boot(), b = boot(), fresh = boot();
    CHECK(a && b && fresh);
    auto before = save(*a);
    CHECK(a->api().track_cc(0, 21, 0) == 1);
    peak(*a, 4410);
    CHECK(save(*a) != before);                      /* A did change */
    peak(*b, 4410);
    peak(*fresh, 4410);
    CHECK(!save(*b).empty() && save(*b) == save(*fresh));
}

static std::string tempDir()
{
    const char* x = std::getenv("XDG_RUNTIME_DIR");
    return x && *x ? x : "/tmp";
}
static int tempFiles()
{
    int n = 0;
    if (DIR* d = opendir(tempDir().c_str())) {
        while (dirent* e = readdir(d))
            n += std::strncmp(e->d_name, "ghoulbox-core-", 14) == 0;
        closedir(d);
    }
    return n;
}
static int mappings()
{
    std::ifstream m("/proc/self/maps");
    std::string line;
    int n = 0;
    while (std::getline(m, line))
        n += line.find("ghoulbox-core-") != std::string::npos;
    return n;
}

static void test_no_temp_leftovers()
{
    for (int i = 0; i < 50; i++) {
        std::string err;
        auto c = CoreInstance::open(LIB, &err);
        CHECK(c);
        CHECK(tempFiles() == 0);                     /* unlinked straight after dlopen */
        CHECK(mappings() > 0);                       /* while open: mapped */
    }
    CHECK(tempFiles() == 0);
    CHECK(mappings() == 0);
}

static void test_missing_symbol_fails()
{
    std::string err;
    auto c = CoreInstance::open("/usr/lib/libm.so.6", &err);   /* a library without gb_* */
    CHECK(!c);
    CHECK(err.find("gb_") != std::string::npos);
    CHECK(tempFiles() == 0 && mappings() == 0);
}

int main()
{
    struct { const char* name; void (*fn)(); } t[] = {
        {"test_two_instances_independent", test_two_instances_independent},
        {"test_state_isolated", test_state_isolated},
        {"test_no_temp_leftovers", test_no_temp_leftovers},
        {"test_missing_symbol_fails", test_missing_symbol_fails},
    };
    for (auto& x : t) {
        int f = fails;
        x.fn();
        std::printf("%s %s\n", fails == f ? "ok  " : "FAIL", x.name);
    }
    std::printf(fails ? "FAIL: %d\n" : "PASS: all\n", fails);
    return fails != 0;
}
