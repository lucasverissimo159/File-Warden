#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace filewarden {

struct ManifestEntry {
    std::filesystem::path relativePath;
    std::string hash;
    std::uintmax_t size = 0;
    std::int64_t mtimeEpochSeconds = 0;
};

// A Manifest records, for one backup snapshot, the content hash of every file
// that was part of it. It is the single source of truth BackupEngine uses to
// figure out which files changed between snapshots, and it's what a restore
// reads to know which objects to pull back out of the store.
//
// The on-disk format is a simple tab-separated text file -- one line per
// entry, deliberately readable with `cat` or `less` rather than a bespoke
// binary format, since a backup tool you can't inspect when something looks
// wrong isn't a backup tool you can trust.
class Manifest {
public:
    std::vector<ManifestEntry> entries;

    void save(const std::filesystem::path& file) const;
    static Manifest load(const std::filesystem::path& file);

    // Builds a relativePath -> entry lookup for O(1) access. BackupEngine
    // calls this once per snapshot rather than linear-scanning entries for
    // every file, which matters once a snapshot has tens of thousands of
    // files in it.
    std::unordered_map<std::string, ManifestEntry> toIndex() const;
};

} // namespace filewarden
