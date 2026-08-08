#pragma once

#include "filewarden/file_scanner.hpp"
#include "filewarden/thread_pool.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace filewarden {

struct DuplicateGroup {
    std::string hash;
    std::uintmax_t fileSize = 0;
    std::vector<std::filesystem::path> files; // every file sharing this content, sorted

    // How much disk space would be reclaimed if every copy but one were removed.
    std::uintmax_t wastedBytes() const {
        return files.size() > 1 ? fileSize * static_cast<std::uintmax_t>(files.size() - 1) : 0;
    }
};

struct DuplicateFinderOptions {
    std::uintmax_t minSize = 1;     // ignore files smaller than this (default: skip 0-byte files)
    size_t partialHashBytes = 4096; // size of the cheap pre-filter read
};

class DuplicateFinder {
public:
    // Finds groups of files with byte-for-byte identical content.
    //
    // This runs in three stages, each one designed to avoid unnecessary disk
    // I/O rather than to add complexity for its own sake:
    //
    //   1. Group by file size. Files with a unique size cannot possibly be
    //      duplicates of anything, so they're discarded before we touch the
    //      disk at all -- on a typical messy folder, this eliminates most
    //      files immediately.
    //   2. Within each size group, hash only the first `partialHashBytes`
    //      bytes of every candidate, in parallel. Two files that differ near
    //      the start are ruled out here without ever reading the rest of a
    //      potentially huge file.
    //   3. Only for files that survive both filters, compute the full-content
    //      hash (also in parallel) to confirm a true duplicate.
    //
    // Results are sorted by reclaimable space (largest first), since that's
    // almost always what a user actually cares about.
    static std::vector<DuplicateGroup> find(const std::vector<FileEntry>& files,
                                             ThreadPool& pool,
                                             const DuplicateFinderOptions& options = {});
};

} // namespace filewarden
