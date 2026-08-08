#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>

namespace filewarden {

// std::filesystem::file_time_type is tied to an unspecified clock in C++17
// (this was only cleanly fixed in C++20 via std::chrono::file_clock::to_sys).
// This helper converts it to a portable Unix epoch timestamp using the
// well-known "now-swap" technique: measure the offset between the file
// clock's "now" and the system clock's "now", and apply that same offset to
// the timestamp being converted.
//
// This is accurate to within a couple of milliseconds (the time between the
// two now() calls), which is more than enough here -- BackupEngine only uses
// this at one-second granularity to decide whether a file's mtime changed
// since the last snapshot.
inline std::int64_t toEpochSeconds(std::filesystem::file_time_type tp) {
    using namespace std::chrono;
    auto sysTime = time_point_cast<system_clock::duration>(
        tp - std::filesystem::file_time_type::clock::now() + system_clock::now());
    return static_cast<std::int64_t>(system_clock::to_time_t(sysTime));
}

} // namespace filewarden
