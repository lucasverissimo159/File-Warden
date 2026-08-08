#include "filewarden/backup_engine.hpp"
#include "filewarden/file_scanner.hpp"
#include "filewarden/huffman.hpp"
#include "filewarden/sha256.hpp"
#include "filewarden/time_utils.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

namespace filewarden {

namespace {

std::string timestampId() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string readWholeFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file: " + path.string());
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void writeWholeFile(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write file: " + path.string());
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("Error writing file: " + path.string());
}

} // namespace

BackupEngine::BackupEngine(fs::path backupRoot, BackupEngineOptions options)
    : backupRoot_(std::move(backupRoot)), options_(options) {
    fs::create_directories(objectsDir());
    fs::create_directories(snapshotsDir());
    fs::create_directories(manifestsDir());
}

fs::path BackupEngine::objectsDir() const { return backupRoot_ / "objects"; }
fs::path BackupEngine::snapshotsDir() const { return backupRoot_ / "snapshots"; }
fs::path BackupEngine::manifestsDir() const { return backupRoot_ / "manifests"; }

fs::path BackupEngine::objectPath(const std::string& hash) const {
    // Shard by the first two hex characters (Git-style) so the store doesn't
    // end up with millions of files dumped into one directory.
    if (hash.size() < 2) {
        throw std::runtime_error("Malformed content hash: " + hash);
    }
    return objectsDir() / hash.substr(0, 2) / hash;
}

fs::path BackupEngine::compressedObjectPath(const std::string& hash) const {
    fs::path p = objectPath(hash);
    p += ".huff";
    return p;
}

bool BackupEngine::objectExists(const std::string& hash) const {
    return fs::exists(objectPath(hash)) || fs::exists(compressedObjectPath(hash));
}

fs::path BackupEngine::resolveExistingObject(const std::string& hash) const {
    // A given piece of content is stored as *either* the raw form or the
    // compressed form, never both -- whichever createSnapshot() decided was
    // smaller at the time it was first written. Callers that just need to
    // read the bytes back (restore, materializing a snapshot link) don't
    // need to care which; they just need to know where it actually lives.
    fs::path compressed = compressedObjectPath(hash);
    if (fs::exists(compressed)) return compressed;
    fs::path raw = objectPath(hash);
    if (fs::exists(raw)) return raw;
    throw std::runtime_error("Object missing from store for hash " + hash + " -- the backup store may be corrupted");
}

std::vector<std::string> BackupEngine::listSnapshots() const {
    std::vector<std::string> ids;
    if (!fs::exists(manifestsDir())) return ids;
    for (const auto& entry : fs::directory_iterator(manifestsDir())) {
        if (entry.path().extension() == ".manifest") {
            ids.push_back(entry.path().stem().string());
        }
    }
    std::sort(ids.begin(), ids.end()); // timestamp format sorts correctly as text
    return ids;
}

std::string BackupEngine::latestSnapshotId() const {
    auto ids = listSnapshots();
    return ids.empty() ? std::string() : ids.back();
}

void BackupEngine::linkOrCopy(const fs::path& from, const fs::path& to) const {
    fs::create_directories(to.parent_path());
    std::error_code ec;
    fs::create_hard_link(from, to, ec);
    if (ec) {
        // Hard links fail across filesystem/device boundaries, and on a
        // handful of filesystems that don't support them at all. Falling
        // back to a real copy means the backup still succeeds -- it just
        // costs more disk space for that one file instead of aborting.
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            throw std::runtime_error("Failed to materialize " + to.string() + ": " + ec.message());
        }
    }
}

SnapshotStats BackupEngine::createSnapshot(const fs::path& source, ThreadPool& pool) {
    SnapshotStats stats;
    stats.snapshotId = timestampId();

    FileScannerOptions scanOpts;
    scanOpts.followSymlinks = options_.followSymlinks;
    auto files = FileScanner::scan(source, scanOpts);

    std::string prevId = latestSnapshotId();
    std::unordered_map<std::string, ManifestEntry> prevIndex;
    if (!prevId.empty()) {
        prevIndex = Manifest::load(manifestsDir() / (prevId + ".manifest")).toIndex();
    }

    // Decide, per file, whether the previous manifest can be trusted (size +
    // mtime unchanged) or whether it needs to be re-hashed. Re-hashing runs
    // in parallel across the whole pending set.
    struct Pending { size_t index; std::future<std::string> future; };
    std::vector<ManifestEntry> newEntries(files.size());
    std::vector<Pending> pending;

    for (size_t i = 0; i < files.size(); ++i) {
        const auto& f = files[i];
        std::string key = f.relativePath.generic_string();
        auto it = prevIndex.find(key);

        bool canTrust = options_.trustMTime
                        && it != prevIndex.end()
                        && it->second.size == f.size
                        && it->second.mtimeEpochSeconds == toEpochSeconds(f.lastWriteTime);

        if (canTrust) {
            newEntries[i] = it->second;
            newEntries[i].relativePath = f.relativePath; // ensure a stable representation
            stats.rehashSkipped++;
        } else {
            pending.push_back(Pending{i, pool.enqueue([path = f.absolutePath]() {
                return Sha256::hashFile(path);
            })});
        }
    }

    for (auto& p : pending) {
        const auto& f = files[p.index];
        ManifestEntry e;
        e.relativePath = f.relativePath;
        e.hash = p.future.get();
        e.size = f.size;
        e.mtimeEpochSeconds = toEpochSeconds(f.lastWriteTime);
        newEntries[p.index] = std::move(e);
    }

    // Materialize the object store and this snapshot's hard links.
    fs::path thisSnapshotDir = snapshotsDir() / stats.snapshotId;
    for (size_t i = 0; i < newEntries.size(); ++i) {
        const auto& e = newEntries[i];
        stats.totalFiles++;
        stats.logicalSize += e.size;

        // Whether bytes need writing is a question about the *content*: has
        // this exact hash ever been stored before, by any file, in any
        // snapshot?
        if (!objectExists(e.hash)) {
            if (options_.compressObjects) {
                std::string raw = readWholeFile(files[i].absolutePath);
                std::string compressed = Huffman::compress(raw);
                if (compressed.size() < raw.size()) {
                    writeWholeFile(compressedObjectPath(e.hash), compressed);
                    stats.bytesWritten += compressed.size();
                    stats.objectsCompressed++;
                } else {
                    // Compression didn't help (common for already-compressed
                    // formats like JPEG or MP4, or for very small files where
                    // the header outweighs any savings) -- store raw instead
                    // of paying a compression tax for nothing.
                    writeWholeFile(objectPath(e.hash), raw);
                    stats.bytesWritten += raw.size();
                }
            } else {
                fs::path obj = objectPath(e.hash);
                fs::create_directories(obj.parent_path());
                std::error_code ec;
                fs::copy_file(files[i].absolutePath, obj, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    throw std::runtime_error("Failed to store object for " + e.relativePath.string() + ": " + ec.message());
                }
                stats.bytesWritten += e.size;
            }
        }

        // Whether a file is "new/changed/unchanged" is a separate question
        // about the *path*: did this path exist in the previous snapshot,
        // and if so, did its content change? A brand-new path whose content
        // happens to duplicate an existing file (like a copy-pasted photo)
        // is still meaningfully "new" from the path's point of view, even
        // though zero new bytes were written for it.
        auto prevIt = prevIndex.find(e.relativePath.generic_string());
        if (prevIt == prevIndex.end()) {
            stats.newFiles++;
        } else if (prevIt->second.hash != e.hash) {
            stats.changedFiles++;
        } else {
            stats.unchangedFiles++;
        }

        linkOrCopy(resolveExistingObject(e.hash), thisSnapshotDir / e.relativePath);
    }

    Manifest m;
    m.entries = std::move(newEntries);
    m.save(manifestsDir() / (stats.snapshotId + ".manifest"));

    return stats;
}

void BackupEngine::restoreSnapshot(const std::string& snapshotId, const fs::path& destination, bool verify) const {
    fs::path manifestPath = manifestsDir() / (snapshotId + ".manifest");
    if (!fs::exists(manifestPath)) {
        throw std::runtime_error("Unknown snapshot: " + snapshotId);
    }
    Manifest m = Manifest::load(manifestPath);

    for (const auto& e : m.entries) {
        fs::path dest = destination / e.relativePath;
        fs::create_directories(dest.parent_path());

        fs::path compressedPath = compressedObjectPath(e.hash);
        if (fs::exists(compressedPath)) {
            std::string raw = Huffman::decompress(readWholeFile(compressedPath));
            writeWholeFile(dest, raw);
        } else {
            fs::path raw = objectPath(e.hash);
            if (!fs::exists(raw)) {
                throw std::runtime_error("Object missing from store for " + e.relativePath.string() +
                                          " (hash " + e.hash + ") -- the backup store may be corrupted");
            }
            std::error_code ec;
            fs::copy_file(raw, dest, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                throw std::runtime_error("Failed to restore " + e.relativePath.string() + ": " + ec.message());
            }
        }

        if (verify) {
            std::string actual = Sha256::hashFile(dest);
            if (actual != e.hash) {
                throw std::runtime_error("Integrity check failed for " + e.relativePath.string() +
                                          " (expected " + e.hash + ", got " + actual + ")");
            }
        }
    }
}

} // namespace filewarden
