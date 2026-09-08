#include "test_framework.hpp"
#include "filewarden/backup_engine.hpp"
#include "filewarden/thread_pool.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace filewarden;
namespace fs = std::filesystem;

namespace {

fs::path freshTempDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("filewarden_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f << content;
}

std::string readFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

} // namespace

TEST(backup_first_snapshot_marks_everything_new) {
    fs::path src = freshTempDir("bk_first_src");
    fs::path store = freshTempDir("bk_first_store");
    writeFile(src / "a.txt", "aaa");
    writeFile(src / "b.txt", "bbb");

    ThreadPool pool(2);
    BackupEngine engine(store);
    auto stats = engine.createSnapshot(src, pool);

    CHECK_EQ(stats.totalFiles, size_t(2));
    CHECK_EQ(stats.newFiles, size_t(2));
    CHECK_EQ(stats.changedFiles, size_t(0));
    CHECK_EQ(stats.unchangedFiles, size_t(0));
}

TEST(backup_second_snapshot_detects_changes_and_additions) {
    fs::path src = freshTempDir("bk_second_src");
    fs::path store = freshTempDir("bk_second_store");
    writeFile(src / "a.txt", "original");
    writeFile(src / "b.txt", "stays the same");

    ThreadPool pool(2);
    BackupEngine engine(store);
    engine.createSnapshot(src, pool);

    std::this_thread::sleep_for(std::chrono::seconds(1)); // ensure a distinct mtime
    writeFile(src / "a.txt", "modified");   // change
    writeFile(src / "c.txt", "brand new");  // addition

    auto stats2 = engine.createSnapshot(src, pool);

    CHECK_EQ(stats2.totalFiles, size_t(3));
    CHECK_EQ(stats2.changedFiles, size_t(1)); // a.txt
    CHECK_EQ(stats2.newFiles, size_t(1));     // c.txt
    CHECK_EQ(stats2.unchangedFiles, size_t(1)); // b.txt
}

TEST(backup_trusts_mtime_to_skip_rehashing_unchanged_files) {
    fs::path src = freshTempDir("bk_trust_src");
    fs::path store = freshTempDir("bk_trust_store");
    writeFile(src / "untouched.txt", "never changes");

    ThreadPool pool(2);
    BackupEngine engine(store);
    engine.createSnapshot(src, pool);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto stats2 = engine.createSnapshot(src, pool);

    CHECK_EQ(stats2.rehashSkipped, size_t(1));
}

TEST(backup_full_rehash_option_disables_mtime_trust) {
    fs::path src = freshTempDir("bk_fullrehash_src");
    fs::path store = freshTempDir("bk_fullrehash_store");
    writeFile(src / "file.txt", "content");

    ThreadPool pool(2);
    BackupEngineOptions opts;
    opts.trustMTime = false;
    BackupEngine engine(store, opts);
    engine.createSnapshot(src, pool);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto stats2 = engine.createSnapshot(src, pool);

    CHECK_EQ(stats2.rehashSkipped, size_t(0));
    CHECK_EQ(stats2.unchangedFiles, size_t(1)); // still correctly detected as unchanged by hash
}

TEST(backup_identical_content_across_files_stored_once) {
    fs::path src = freshTempDir("bk_dedup_src");
    fs::path store = freshTempDir("bk_dedup_store");
    writeFile(src / "one.txt", "shared content");
    writeFile(src / "two.txt", "shared content");
    writeFile(src / "three.txt", "different content");

    ThreadPool pool(2);
    BackupEngine engine(store);
    auto stats = engine.createSnapshot(src, pool);

    // Only 2 distinct contents exist, so only 2 files' worth of bytes should
    // actually be written to the object store, even though 3 files exist.
    size_t objectCount = 0;
    for (auto& p : fs::recursive_directory_iterator(store / "objects")) {
        if (p.is_regular_file()) objectCount++;
    }
    CHECK_EQ(objectCount, size_t(2));
    CHECK_TRUE(stats.bytesWritten < stats.logicalSize);
}

TEST(backup_restore_reproduces_exact_content) {
    fs::path src = freshTempDir("bk_restore_src");
    fs::path store = freshTempDir("bk_restore_store");
    fs::path dest = freshTempDir("bk_restore_dest");
    writeFile(src / "nested" / "deep.txt", "deep content");
    writeFile(src / "top.txt", "top content");

    ThreadPool pool(2);
    BackupEngine engine(store);
    auto stats = engine.createSnapshot(src, pool);
    engine.restoreSnapshot(stats.snapshotId, dest, /*verify=*/true);

    CHECK_EQ(readFile(dest / "nested" / "deep.txt"), std::string("deep content"));
    CHECK_EQ(readFile(dest / "top.txt"), std::string("top content"));
}

TEST(backup_restore_removes_files_not_in_snapshot) {
    fs::path src = freshTempDir("bk_restore_cleanup_src");
    fs::path store = freshTempDir("bk_restore_cleanup_store");
    fs::path dest = freshTempDir("bk_restore_cleanup_dest");
    writeFile(src / "kept.txt", "kept");
    writeFile(dest / "stale.txt", "must be removed");
    writeFile(dest / "nested" / "stale.txt", "must also be removed");

    ThreadPool pool(2);
    BackupEngine engine(store);
    auto stats = engine.createSnapshot(src, pool);
    engine.restoreSnapshot(stats.snapshotId, dest, /*verify=*/true);

    CHECK_TRUE(fs::exists(dest / "kept.txt"));
    CHECK_TRUE(!fs::exists(dest / "stale.txt"));
    CHECK_TRUE(!fs::exists(dest / "nested"));
}

TEST(backup_restore_of_older_snapshot_excludes_later_additions) {
    fs::path src = freshTempDir("bk_pointintime_src");
    fs::path store = freshTempDir("bk_pointintime_store");
    fs::path dest = freshTempDir("bk_pointintime_dest");
    writeFile(src / "a.txt", "a");

    ThreadPool pool(2);
    BackupEngine engine(store);
    auto stats1 = engine.createSnapshot(src, pool);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    writeFile(src / "b.txt", "b"); // added after snapshot 1
    engine.createSnapshot(src, pool);

    engine.restoreSnapshot(stats1.snapshotId, dest);

    CHECK_TRUE(fs::exists(dest / "a.txt"));
    CHECK_TRUE(!fs::exists(dest / "b.txt")); // must not leak from the later snapshot
}

TEST(backup_list_snapshots_returns_sorted_ids) {
    fs::path src = freshTempDir("bk_list_src");
    fs::path store = freshTempDir("bk_list_store");
    writeFile(src / "a.txt", "a");

    ThreadPool pool(2);
    BackupEngine engine(store);
    engine.createSnapshot(src, pool);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    engine.createSnapshot(src, pool);

    auto ids = engine.listSnapshots();
    CHECK_EQ(ids.size(), size_t(2));
    CHECK_TRUE(ids[0] < ids[1]);
}

TEST(backup_consecutive_snapshots_get_unique_ids) {
    fs::path src = freshTempDir("bk_collision_src");
    fs::path store = freshTempDir("bk_collision_store");
    writeFile(src / "a.txt", "a");

    ThreadPool pool(2);
    BackupEngine engine(store);
    auto first = engine.createSnapshot(src, pool);
    auto second = engine.createSnapshot(src, pool);

    CHECK_TRUE(first.snapshotId != second.snapshotId);
    CHECK_EQ(engine.listSnapshots().size(), size_t(2));
    CHECK_TRUE(fs::exists(store / "manifests" / (first.snapshotId + ".manifest")));
    CHECK_TRUE(fs::exists(store / "manifests" / (second.snapshotId + ".manifest")));
}

TEST(backup_restore_unknown_snapshot_throws) {
    fs::path store = freshTempDir("bk_unknown_store");
    BackupEngine engine(store);
    CHECK_THROWS(engine.restoreSnapshot("no_such_snapshot", freshTempDir("bk_unknown_dest")));
}

TEST(backup_with_compression_round_trips_correctly) {
    fs::path src = freshTempDir("bk_compress_src");
    fs::path store = freshTempDir("bk_compress_store");
    fs::path dest = freshTempDir("bk_compress_dest");

    // Repetitive, compressible content -- large enough to comfortably clear
    // the Huffman header overhead.
    std::string compressible;
    for (int i = 0; i < 500; ++i) compressible += "the quick brown fox jumps over the lazy dog. ";
    writeFile(src / "compressible.txt", compressible);

    ThreadPool pool(2);
    BackupEngineOptions opts;
    opts.compressObjects = true;
    BackupEngine engine(store, opts);
    auto stats = engine.createSnapshot(src, pool);

    CHECK_EQ(stats.objectsCompressed, size_t(1));
    CHECK_TRUE(stats.bytesWritten < stats.logicalSize); // compression actually saved bytes

    engine.restoreSnapshot(stats.snapshotId, dest, /*verify=*/true);
    CHECK_EQ(readFile(dest / "compressible.txt"), compressible);
}

TEST(backup_with_compression_falls_back_to_raw_for_incompressible_files) {
    fs::path src = freshTempDir("bk_nocompress_src");
    fs::path store = freshTempDir("bk_nocompress_store");
    fs::path dest = freshTempDir("bk_nocompress_dest");

    // A short file: compression would make it *larger* (fixed header cost),
    // so BackupEngine should detect that and store it raw instead.
    writeFile(src / "tiny.txt", "hi");

    ThreadPool pool(2);
    BackupEngineOptions opts;
    opts.compressObjects = true;
    BackupEngine engine(store, opts);
    auto stats = engine.createSnapshot(src, pool);

    CHECK_EQ(stats.objectsCompressed, size_t(0)); // fell back to raw, not compressed
    CHECK_EQ(stats.bytesWritten, stats.logicalSize); // raw storage costs exactly the original size

    engine.restoreSnapshot(stats.snapshotId, dest, /*verify=*/true);
    CHECK_EQ(readFile(dest / "tiny.txt"), std::string("hi"));
}

TEST(backup_compressed_and_uncompressed_stores_are_independently_restorable) {
    // A backup created without compression must remain fully restorable even
    // though the object store format now supports two variants -- restore
    // must not assume every object is compressed just because the *option*
    // exists in the codebase.
    fs::path src = freshTempDir("bk_mixed_src");
    fs::path store = freshTempDir("bk_mixed_store");
    fs::path dest = freshTempDir("bk_mixed_dest");
    writeFile(src / "a.txt", "plain content, no compression requested");

    ThreadPool pool(2);
    BackupEngine engine(store); // compressObjects defaults to false
    auto stats = engine.createSnapshot(src, pool);
    engine.restoreSnapshot(stats.snapshotId, dest, /*verify=*/true);

    CHECK_EQ(readFile(dest / "a.txt"), std::string("plain content, no compression requested"));
}
