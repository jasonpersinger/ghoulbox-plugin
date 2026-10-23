/* SPDX-License-Identifier: GPL-3.0-only */
/* A test binary's own XDG_RUNTIME_DIR (where CoreInstance puts its copies): counting ghoulbox-core-* files there sees
 * only this process's, even while other tests run in parallel. Removed at exit; rmdir fails if anything was left. */
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

struct PrivateRuntimeDir {
    std::string path;
    PrivateRuntimeDir()
    {
        char tmpl[] = "build/rt-XXXXXX";
        char* real = mkdtemp(tmpl) ? realpath(tmpl, nullptr) : nullptr;
        if (!real) {
            std::perror("PrivateRuntimeDir");
            std::exit(2);
        }
        path = real;
        std::free(real);
        setenv("XDG_RUNTIME_DIR", path.c_str(), 1);
    }
    ~PrivateRuntimeDir()
    {
        if (rmdir(path.c_str()) != 0)
            std::fprintf(stderr, "  leftovers in %s\n", path.c_str());
    }
};
