/* SPDX-License-Identifier: GPL-3.0-only */
/* The SAVE library every instance shares: the device's flash, as one file in the user's data folder. Each instance
 * commits only the 4096-byte sectors it changed, merged under a lock into what the file holds now. */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class Library {
public:
    static std::string defaultPath();      // $XDG_DATA_HOME or ~/.local/share, + "/GHOULBOX/library.nor"
    explicit Library(std::string path) : path_(std::move(path)) {}
    std::vector<uint8_t> load(uint32_t size);   // missing → size bytes of 0xFF; wrong size → moved to
                                                // path + ".bad-<unixtime>", and erased
    // writes the sectors where `current` differs from `base` into the file, under flock(LOCK_EX) on path + ".lock";
    // re-reads the file first, so other instances' sectors survive; atomic temp + rename
    bool commit(const std::vector<uint8_t>& base, const std::vector<uint8_t>& current);

private:
    std::string path_;
};
