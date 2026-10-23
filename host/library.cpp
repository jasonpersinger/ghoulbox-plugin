/* SPDX-License-Identifier: GPL-3.0-only */
#include "library.h"
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static constexpr uint32_t SECTOR = 4096;

std::string Library::defaultPath()
{
    const char* x = std::getenv("XDG_DATA_HOME");
    const char* h = std::getenv("HOME");
    std::string base = x && *x ? x : std::string(h ? h : "") + "/.local/share";
    return base + "/GHOULBOX/library.nor";
}

static void makeDirs(const std::string& file)
{
    for (size_t i = file.find('/', 1); i != std::string::npos; i = file.find('/', i + 1))
        mkdir(file.substr(0, i).c_str(), 0755);
}

enum { IMG_OK = 0, IMG_MISSING = -1, IMG_BAD_SIZE = -2, IMG_UNREADABLE = -3 };

/* the whole file if it is exactly `size` bytes. IMG_UNREADABLE (EACCES, EMFILE, EIO...) is never taken for missing or
 * for the wrong size: either would overwrite or move away a library that is fine */
static int readImage(const std::string& path, uint32_t size, std::vector<uint8_t>& img)
{
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? IMG_MISSING : IMG_UNREADABLE;
    struct stat st;
    int rc = fstat(fd, &st) != 0 ? IMG_UNREADABLE : st.st_size != (off_t)size ? IMG_BAD_SIZE : IMG_OK;
    img.assign(size, 0xFF);
    for (uint32_t got = 0; rc == IMG_OK && got < size;) {
        ssize_t n = read(fd, img.data() + got, size - got);
        if (n <= 0)
            rc = IMG_UNREADABLE;
        else
            got += (uint32_t)n;
    }
    close(fd);
    return rc;
}

std::vector<uint8_t> Library::load(uint32_t size)
{
    std::vector<uint8_t> img;
    int rc = readImage(path_, size, img);
    if (rc == IMG_BAD_SIZE)
        rename(path_.c_str(), (path_ + ".bad-" + std::to_string((long long)std::time(nullptr))).c_str());
    if (rc != IMG_OK)
        img.assign(size, 0xFF);                     // (unreadable: erased this time, the file left alone)
    return img;
}

static bool writeAll(int fd, const uint8_t* p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w <= 0)
            return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

bool Library::commit(const std::vector<uint8_t>& base, const std::vector<uint8_t>& current)
{
    if (base.size() != current.size() || current.empty())
        return false;
    makeDirs(path_);
    int lock = open((path_ + ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lock < 0)
        return false;
    bool ok = flock(lock, LOCK_EX) == 0;
    std::vector<uint8_t> img;
    if (ok) {
        int rc = readImage(path_, (uint32_t)current.size(), img);
        if (rc == IMG_MISSING || rc == IMG_BAD_SIZE)
            img.assign(current.size(), 0xFF);                 // (none yet, or load() moved a bad one aside: erased)
        else
            ok = rc == IMG_OK;                                // unreadable: fail, the worker retries
    }
    for (size_t s = 0; ok && s < current.size(); s += SECTOR) {
        size_t n = std::min<size_t>(SECTOR, current.size() - s);
        if (std::equal(current.begin() + s, current.begin() + s + n, base.begin() + s))
            continue;                                           // not this instance's: the file's stands
        std::copy(current.begin() + s, current.begin() + s + n, img.begin() + s);
    }
    std::string tmp = path_ + ".tmp-XXXXXX";
    int fd = ok ? mkstemp(&tmp[0]) : -1;
    ok = fd >= 0 && writeAll(fd, img.data(), img.size()) && fsync(fd) == 0;
    if (fd >= 0)
        ok = close(fd) == 0 && ok;
    ok = ok && rename(tmp.c_str(), path_.c_str()) == 0;
    if (!ok && fd >= 0)
        unlink(tmp.c_str());
    if (ok) {                                                   // the rename itself, durable
        int d = open(path_.substr(0, path_.rfind('/')).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (d >= 0) {
            fsync(d);
            close(d);
        }
    }
    close(lock);                                                // (releases the flock)
    return ok;
}
