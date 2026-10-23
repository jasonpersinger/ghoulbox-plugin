/* SPDX-License-Identifier: GPL-3.0-only */
/* Task 6: the SAVE library every instance shares (the device's flash, on disk). */
#include "../host/library.h"
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "  %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; return; } } while (0)

static const uint32_t SIZE = 0x100000, SECTOR = 4096;
static std::string dir;
static std::string fresh(const char* name)
{
    std::string p = dir + "/" + name + "/library.nor";
    return p;
}
static std::vector<uint8_t> erased() { return std::vector<uint8_t>(SIZE, 0xFF); }
static std::vector<uint8_t> readFile(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}
static bool sectorIs(const std::vector<uint8_t>& img, uint32_t s, uint8_t v)
{
    for (uint32_t i = 0; i < SECTOR; i++)
        if (img[s * SECTOR + i] != v)
            return false;
    return true;
}
static std::vector<uint8_t> withSector(std::vector<uint8_t> img, uint32_t s, uint8_t v)
{
    std::fill(img.begin() + s * SECTOR, img.begin() + (s + 1) * SECTOR, v);
    return img;
}

static void test_default_path()
{
    setenv("XDG_DATA_HOME", "/x/data", 1);
    CHECK(Library::defaultPath() == "/x/data/GHOULBOX/library.nor");
    unsetenv("XDG_DATA_HOME");
    setenv("HOME", "/home/someone", 1);
    CHECK(Library::defaultPath() == "/home/someone/.local/share/GHOULBOX/library.nor");
}

static void test_missing_is_erased()
{
    Library lib(fresh("missing"));
    CHECK(lib.load(SIZE) == erased());
}

static void test_bad_size_backed_up()
{
    std::string p = fresh("bad");
    mkdir(p.substr(0, p.rfind('/')).c_str(), 0700);
    { std::ofstream f(p, std::ios::binary); f << "not a flash image"; }
    Library lib(p);
    CHECK(lib.load(SIZE) == erased());
    CHECK(access(p.c_str(), F_OK) != 0);            /* moved aside */
    int backups = 0;
    if (DIR* d = opendir(p.substr(0, p.rfind('/')).c_str())) {
        while (dirent* e = readdir(d))
            if (std::string(e->d_name).rfind("library.nor.bad-", 0) == 0) {
                backups++;
                CHECK(readFile(p.substr(0, p.rfind('/')) + "/" + e->d_name).size() == 17);
            }
        closedir(d);
    }
    CHECK(backups == 1);
}

static void test_commit_roundtrip()
{
    Library lib(fresh("roundtrip"));
    auto base = lib.load(SIZE), cur = withSector(base, 3, 0x42);
    CHECK(lib.commit(base, cur));
    CHECK(lib.load(SIZE) == cur);
    CHECK(readFile(fresh("roundtrip")).size() == SIZE);
}

static void test_concurrent_commits_keep_both()
{
    std::string p = fresh("concurrent");
    pid_t kids[2];
    for (int k = 0; k < 2; k++)
        if (!(kids[k] = fork())) {                  /* two instances, the same starting flash, a sector each */
            Library lib(p);
            auto base = erased();
            for (int i = 1; i <= 100; i++) {
                auto cur = withSector(base, 10 + k, (uint8_t)i);
                if (!lib.commit(base, cur))
                    _exit(1);
                base = cur;
            }
            _exit(0);
        }
    for (pid_t k : kids) {
        int st;
        waitpid(k, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }
    auto img = Library(p).load(SIZE);
    CHECK(sectorIs(img, 10, 100) && sectorIs(img, 11, 100));
}

static void test_same_sector_last_wins()
{
    std::string p = fresh("same");
    Library a(p), b(p);
    CHECK(a.commit(erased(), withSector(erased(), 5, 1)));
    CHECK(b.commit(erased(), withSector(erased(), 5, 2)));
    CHECK(sectorIs(a.load(SIZE), 5, 2));
}

static void test_commit_rejects_mismatched_sizes()
{
    Library lib(fresh("mismatch"));
    CHECK(!lib.commit(erased(), std::vector<uint8_t>(SIZE - 1, 0)));
}

int main()
{
    char tmpl[] = "build/libtest-XXXXXX";
    if (!mkdtemp(tmpl))
        return 2;
    dir = tmpl;
    struct { const char* name; void (*fn)(); } t[] = {
        {"test_default_path", test_default_path},
        {"test_missing_is_erased", test_missing_is_erased},
        {"test_bad_size_backed_up", test_bad_size_backed_up},
        {"test_commit_roundtrip", test_commit_roundtrip},
        {"test_concurrent_commits_keep_both", test_concurrent_commits_keep_both},
        {"test_same_sector_last_wins", test_same_sector_last_wins},
        {"test_commit_rejects_mismatched_sizes", test_commit_rejects_mismatched_sizes},
    };
    for (auto& x : t) {
        int f = fails;
        x.fn();
        std::printf("%s %s\n", fails == f ? "ok  " : "FAIL", x.name);
        std::fflush(stdout);
    }
    std::printf(fails ? "FAIL: %d\n" : "PASS: all\n", fails);
    if (!fails)
        std::system(("rm -rf " + dir).c_str());
    return fails != 0;
}
