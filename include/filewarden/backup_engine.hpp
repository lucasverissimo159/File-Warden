#pragma once

#include "filewarden/manifest.hpp"
#include "filewarden/thread_pool.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace filewarden {

struct SnapshotStats {
    std::string snapshotId;
    size_t totalFiles = 0;
    size_t newFiles = 0;       // content not seen in the store before -> actually written
    size_t changedFiles = 0;   // path existed before, content hash differs from last snapshot
    size_t unchangedFiles = 0; // path existed before, content hash identical -> just re-linked
    size_t rehashSkipped = 0;  // files trusted via size+mtime match, not rehashed at all
    size_t objectsCompressed = 0; // new objects that were actually stored in compressed form
    std::uintmax_t logicalSize = 0; // sum of sizes of every file in this snapshot
    std::uintmax_t bytesWritten = 0; // bytes actually newly written to the object store (post-compression)
};

struct BackupEngineOptions {
    // If true (default), a file whose size and modification time exactly
    // match the previous snapshot's manifest entry is trusted without being
    // re-hashed -- the same optimization rsync uses. This turns repeated
    // backups of a mostly-unchanged tree from "re-read everything" into
    // "re-read only what's new," which is the whole point of an incremental
    // backup tool.
    //
    // The tradeoff: a program that rewrites a file with different content
    // while preserving its original mtime (unusual, but possible) would be
    // missed. Set this to false to always verify by content instead.
    bool trustMTime = true;
    bool followSymlinks = false;

    // If true, new objects are Huffman-compressed before being written to
    // the store. FileWarden always compares the compressed size against the
    // original and keeps whichever is smaller -- so turning this on can only
    // help or be a no-op, never make storage worse, at the cost of reading
    // each new file into memory once (compression here operates on whole
    // buffers, not streamed, unlike hashing).
    //
    // Note this changes what lives in snapshots/<id>/: with compression on,
    // a compressed object can't be hard-linked into a snapshot folder as a
    // directly readable file the way an uncompressed one can. Use `restore`
    // to get real files back; that's true either way, but it's no longer
    // just a convenience when compression is enabled.
    bool compressObjects = false;
};

// BackupEngine stores snapshots in a content-addressable object store:
// each unique piece of content is written to disk exactly once, named by its
// SHA-256 hash (objects/<hash prefix>/<hash>, sharded the way Git shards its
// object store). A snapshot is then just a directory of hard links pointing
// into that store -- so a file that hasn't changed since the last snapshot
// costs zero additional disk space, and a file that's identical to some
// *other* file anywhere else in the backup history is only ever stored once.
// This is the same idea behind tools like Time Machine, Borg and restic,
// simplified down to something you can read start to finish in one sitting.
class BackupEngine {
public:
    explicit BackupEngine(std::filesystem::path backupRoot, BackupEngineOptions options = {});

    // Scans `source`, stores any new/changed content, and creates a new
    // snapshot recording every file at its current state.
    SnapshotStats createSnapshot(const std::filesystem::path& source, ThreadPool& pool);

    // Restores every file recorded in `snapshotId` into `destination`,
    // recreating the relative directory structure. Compressed and
    // uncompressed objects are both handled transparently. If `verify` is
    // true, each restored file is re-hashed and compared against the
    // manifest so silent corruption (or a compression bug) is caught instead
    // of silently restored.
    void restoreSnapshot(const std::string& snapshotId, const std::filesystem::path& destination, bool verify = false) const;

    // Returns snapshot IDs (timestamps), oldest first.
    std::vector<std::string> listSnapshots() const;

private:
    std::filesystem::path objectsDir() const;
    std::filesystem::path snapshotsDir() const;
    std::filesystem::path manifestsDir() const;
    std::filesystem::path objectPath(const std::string& hash) const;
    std::filesystem::path compressedObjectPath(const std::string& hash) const;
    std::filesystem::path resolveExistingObject(const std::string& hash) const;
    bool objectExists(const std::string& hash) const;
    std::string latestSnapshotId() const;

    void linkOrCopy(const std::filesystem::path& from, const std::filesystem::path& to) const;

    std::filesystem::path backupRoot_;
    BackupEngineOptions options_;
};

} // namespace filewarden
