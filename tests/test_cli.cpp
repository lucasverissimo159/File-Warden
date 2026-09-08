#include "test_framework.hpp"
#include "filewarden/cli.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
    std::ofstream out(path, std::ios::binary);
    out << content;
}

int runCommand(const std::vector<std::string>& arguments) {
    std::vector<std::string> storage = arguments;
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& argument : storage) argv.push_back(&argument[0]);
    return runCli(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST(cli_rejects_unknown_options) {
    fs::path root = freshTempDir("cli_unknown_option");
    CHECK_EQ(runCommand({"filewarden", "scan", root.string(), "--jsoon"}), 1);
}

TEST(cli_rejects_extra_positionals) {
    fs::path root = freshTempDir("cli_extra_positional");
    fs::path store = freshTempDir("cli_extra_store");
    CHECK_EQ(runCommand({"filewarden", "backup", root.string(), store.string(), "unexpected"}), 1);
}

TEST(cli_rejects_missing_option_value) {
    fs::path root = freshTempDir("cli_missing_value");
    CHECK_EQ(runCommand({"filewarden", "dedupe", root.string(), "--threads"}), 1);
}

TEST(cli_quarantine_preserves_existing_destination) {
    fs::path root = freshTempDir("cli_quarantine_src");
    fs::path quarantine = freshTempDir("cli_quarantine_dest");
    writeFile(root / "a_keep.txt", "same content");
    writeFile(root / "z_duplicate.txt", "same content");
    writeFile(quarantine / "z_duplicate.txt", "do not overwrite");

    CHECK_EQ(runCommand({"filewarden", "dedupe", root.string(), "--keep", "first-path", "--quarantine", quarantine.string()}), 0);
    CHECK_EQ(std::string("do not overwrite"), [&] {
        std::ifstream in(quarantine / "z_duplicate.txt", std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }());
    CHECK_TRUE(fs::exists(quarantine / "z_duplicate.txt.1"));
    CHECK_TRUE(!fs::exists(root / "z_duplicate.txt"));
}
