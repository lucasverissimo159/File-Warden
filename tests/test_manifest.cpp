#include "test_framework.hpp"
#include "filewarden/manifest.hpp"

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

void writeText(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

} // namespace

TEST(manifest_rejects_malformed_entry) {
    fs::path dir = freshTempDir("manifest_malformed");
    fs::path file = dir / "broken.manifest";
    writeText(file, "# filewarden manifest v1\nnot-a-valid-entry\n");

    CHECK_THROWS(Manifest::load(file));
}

TEST(manifest_rejects_invalid_numeric_fields) {
    fs::path dir = freshTempDir("manifest_invalid_number");
    fs::path file = dir / "broken.manifest";
    writeText(file, "hash\tnot-a-size\t0\tfile.txt\n");

    CHECK_THROWS(Manifest::load(file));
}

TEST(manifest_round_trips_escaped_paths) {
    fs::path dir = freshTempDir("manifest_escaped_path");
    fs::path file = dir / "roundtrip.manifest";
    Manifest original;
    ManifestEntry entry;
    entry.hash = std::string(64, 'a');
    entry.size = 12;
    entry.mtimeEpochSeconds = 42;
    entry.relativePath = "folder/name\twith\ncontrols.txt";
    original.entries.push_back(entry);

    original.save(file);
    auto loaded = Manifest::load(file);

    CHECK_EQ(loaded.entries.size(), size_t(1));
    CHECK_EQ(loaded.entries[0].relativePath.generic_string(), entry.relativePath.generic_string());
}
