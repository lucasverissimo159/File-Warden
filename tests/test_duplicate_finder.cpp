#include "test_framework.hpp"
#include "filewarden/file_scanner.hpp"
#include "filewarden/duplicate_finder.hpp"
#include "filewarden/thread_pool.hpp"

#include <filesystem>
#include <fstream>

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

} // namespace

TEST(scanner_finds_all_regular_files_recursively) {
    fs::path dir = freshTempDir("scanner_recursive");
    writeFile(dir / "a.txt", "a");
    writeFile(dir / "sub" / "b.txt", "b");
    writeFile(dir / "sub" / "deeper" / "c.txt", "c");

    auto files = FileScanner::scan(dir);
    CHECK_EQ(files.size(), size_t(3));
}

TEST(scanner_throws_on_nonexistent_path) {
    CHECK_THROWS(FileScanner::scan("/this/path/definitely/does/not/exist/anywhere"));
}

TEST(scanner_relative_paths_are_relative_to_root) {
    fs::path dir = freshTempDir("scanner_relative");
    writeFile(dir / "sub" / "file.txt", "x");

    auto files = FileScanner::scan(dir);
    CHECK_EQ(files.size(), size_t(1));
    CHECK_EQ(files[0].relativePath.generic_string(), std::string("sub/file.txt"));
}

TEST(duplicate_finder_groups_identical_content) {
    fs::path dir = freshTempDir("dup_identical");
    writeFile(dir / "a.txt", "duplicate content here");
    writeFile(dir / "b.txt", "duplicate content here");
    writeFile(dir / "c.txt", "totally different");

    auto files = FileScanner::scan(dir);
    ThreadPool pool(2);
    auto groups = DuplicateFinder::find(files, pool);

    CHECK_EQ(groups.size(), size_t(1));
    CHECK_EQ(groups[0].files.size(), size_t(2));
}

TEST(duplicate_finder_ignores_unique_files) {
    fs::path dir = freshTempDir("dup_unique_only");
    writeFile(dir / "a.txt", "aaa");
    writeFile(dir / "b.txt", "bbb");
    writeFile(dir / "c.txt", "ccc");

    auto files = FileScanner::scan(dir);
    ThreadPool pool(2);
    auto groups = DuplicateFinder::find(files, pool);

    CHECK_TRUE(groups.empty());
}

TEST(duplicate_finder_same_size_different_content_not_grouped) {
    // Regression test for the partial-hash stage: two files of identical
    // size but different bytes must NOT be reported as duplicates.
    fs::path dir = freshTempDir("dup_same_size");
    writeFile(dir / "a.txt", "AAAAAAAAAA");
    writeFile(dir / "b.txt", "BBBBBBBBBB");

    auto files = FileScanner::scan(dir);
    ThreadPool pool(2);
    auto groups = DuplicateFinder::find(files, pool);

    CHECK_TRUE(groups.empty());
}

TEST(duplicate_finder_handles_files_smaller_than_partial_window) {
    // Files smaller than partialHashBytes take the "whole file fits" shortcut
    // path in DuplicateFinder -- make sure that path is also correct.
    fs::path dir = freshTempDir("dup_small_files");
    writeFile(dir / "a.txt", "hi");
    writeFile(dir / "b.txt", "hi");

    auto files = FileScanner::scan(dir);
    ThreadPool pool(2);
    DuplicateFinderOptions opts;
    opts.partialHashBytes = 4096; // both files are well under this
    auto groups = DuplicateFinder::find(files, pool, opts);

    CHECK_EQ(groups.size(), size_t(1));
}

TEST(duplicate_finder_respects_min_size) {
    fs::path dir = freshTempDir("dup_min_size");
    writeFile(dir / "a.txt", "same");
    writeFile(dir / "b.txt", "same");

    auto files = FileScanner::scan(dir);
    ThreadPool pool(2);
    DuplicateFinderOptions opts;
    opts.minSize = 1000; // larger than the test files, so both should be excluded
    auto groups = DuplicateFinder::find(files, pool, opts);

    CHECK_TRUE(groups.empty());
}

TEST(duplicate_finder_wasted_bytes_calculation) {
    fs::path dir = freshTempDir("dup_wasted_bytes");
    writeFile(dir / "a.txt", "12345"); // 5 bytes
    writeFile(dir / "b.txt", "12345");
    writeFile(dir / "c.txt", "12345");

    auto files = FileScanner::scan(dir);
    ThreadPool pool(2);
    auto groups = DuplicateFinder::find(files, pool);

    CHECK_EQ(groups.size(), size_t(1));
    // 3 copies of a 5-byte file -> 2 copies are "wasted" -> 10 bytes reclaimable.
    CHECK_EQ(groups[0].wastedBytes(), std::uintmax_t(10));
}

TEST(duplicate_finder_multiple_independent_groups) {
    fs::path dir = freshTempDir("dup_multi_group");
    writeFile(dir / "a1.txt", "group A content");
    writeFile(dir / "a2.txt", "group A content");
    writeFile(dir / "b1.txt", "group B content!!");
    writeFile(dir / "b2.txt", "group B content!!");
    writeFile(dir / "unique.txt", "nothing else like me");

    auto files = FileScanner::scan(dir);
    ThreadPool pool(4);
    auto groups = DuplicateFinder::find(files, pool);

    CHECK_EQ(groups.size(), size_t(2));
}
