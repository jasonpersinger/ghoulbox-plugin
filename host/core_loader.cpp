/* SPDX-License-Identifier: GPL-3.0-only */
#include "core_loader.h"
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

static std::string tempDir()
{
    const char* x = std::getenv("XDG_RUNTIME_DIR");
    return x && *x ? x : "/tmp";
}

/* libPath copied to a new temp file; its path, or "" */
static std::string privateCopy(const std::string& libPath, std::string* err)
{
    std::string path = tempDir() + "/ghoulbox-core-XXXXXX";
    int out = mkstemp(&path[0]);
    if (out < 0) {
        *err = "mkstemp failed in " + tempDir();
        return "";
    }
    int in = ::open(libPath.c_str(), O_RDONLY | O_CLOEXEC);
    bool ok = in >= 0;
    char buf[1 << 16];
    for (ssize_t n; ok && (n = read(in, buf, sizeof buf)) != 0;)
        ok = n > 0 && write(out, buf, (size_t)n) == n;
    if (in >= 0)
        close(in);
    ok = close(out) == 0 && ok;
    if (!ok) {
        unlink(path.c_str());
        *err = "cannot copy " + libPath;
        return "";
    }
    return path;
}

std::unique_ptr<CoreInstance> CoreInstance::open(const std::string& libPath, std::string* err)
{
    std::string copy = privateCopy(libPath, err);
    if (copy.empty())
        return nullptr;
    void* h = dlopen(copy.c_str(), RTLD_NOW | RTLD_LOCAL);
    unlink(copy.c_str());                       /* the mapping outlives the name: nothing left behind, even on a crash */
    if (!h) {
        const char* e = dlerror();
        *err = e ? e : "dlopen failed";
        return nullptr;
    }
    std::unique_ptr<CoreInstance> c(new CoreInstance);
    c->handle_ = h;
#define GB_SYM(name, ret, args)                                                \
    if (!(c->api_.name = reinterpret_cast<ret(*) args>(dlsym(h, "gb_" #name)))) { \
        *err = "missing symbol gb_" #name;                                     \
        return nullptr;                                                        \
    }
    GB_API_LIST(GB_SYM)
#undef GB_SYM
    return c;
}

CoreInstance::~CoreInstance()
{
    if (handle_)
        dlclose(handle_);
}
