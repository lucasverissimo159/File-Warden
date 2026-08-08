#include "filewarden/cli.hpp"
#include "filewarden/backup_engine.hpp"
#include "filewarden/duplicate_finder.hpp"
#include "filewarden/file_scanner.hpp"
#include "filewarden/manifest.hpp"
#include "filewarden/thread_pool.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define FW_ISATTY _isatty
#define FW_FILENO _fileno
#else
#include <unistd.h>
#define FW_ISATTY isatty
#define FW_FILENO fileno
#endif

namespace fs = std::filesystem;

namespace filewarden {
namespace {

// ---------------------------------------------------------------------
// Small formatting helpers
// ---------------------------------------------------------------------

bool colorEnabled() {
    static bool tty = FW_ISATTY(FW_FILENO(stdout)) != 0;
    return tty;
}

std::string bold(const std::string& s) { return colorEnabled() ? "\033[1m" + s + "\033[0m" : s; }
std::string green(const std::string& s) { return colorEnabled() ? "\033[32m" + s + "\033[0m" : s; }
std::string yellow(const std::string& s) { return colorEnabled() ? "\033[33m" + s + "\033[0m" : s; }
std::string cyan(const std::string& s) { return colorEnabled() ? "\033[36m" + s + "\033[0m" : s; }
std::string dim(const std::string& s) { return colorEnabled() ? "\033[2m" + s + "\033[0m" : s; }

std::string humanBytes(std::uintmax_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << " " << units[unit];
    return oss.str();
}

// ---------------------------------------------------------------------
// Minimal JSON output support (write-only -- FileWarden never needs to
// parse JSON, only emit it for --json mode, which is a much smaller and
// lower-risk problem than a general parser).
// ---------------------------------------------------------------------

std::string jsonEscape(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += "\"";
    return out;
}

std::string jsonEscape(const fs::path& p) { return jsonEscape(p.string()); }

void moveFile(const fs::path& from, const fs::path& to) {
    fs::create_directories(to.parent_path());
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) {
        // rename() fails across filesystem boundaries (EXDEV); fall back to
        // copy + remove so the move still succeeds either way.
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error("Failed to move " + from.string() + ": " + ec.message());
        fs::remove(from);
    }
}

fs::path chooseKeepPath(const std::vector<fs::path>& files, const std::string& strategy) {
    auto byMTime = [](const fs::path& a, const fs::path& b) {
        std::error_code ecA, ecB;
        return fs::last_write_time(a, ecA) < fs::last_write_time(b, ecB);
    };
    if (strategy == "newest") {
        return *std::max_element(files.begin(), files.end(), byMTime);
    }
    if (strategy == "first-path") {
        return *std::min_element(files.begin(), files.end());
    }
    if (strategy == "oldest") {
        return *std::min_element(files.begin(), files.end(), byMTime);
    }
    throw std::runtime_error("Unknown --keep strategy '" + strategy + "' (expected: oldest, newest, first-path)");
}

// ---------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------

class ArgParser {
public:
    explicit ArgParser(std::vector<std::string> args) : raw_(std::move(args)) {}

    std::optional<std::string> option(const std::string& name) const {
        for (size_t i = 0; i < raw_.size(); ++i) {
            if (raw_[i] == name && i + 1 < raw_.size()) return raw_[i + 1];
        }
        return std::nullopt;
    }

    bool flag(const std::string& name) const {
        return std::find(raw_.begin(), raw_.end(), name) != raw_.end();
    }

    std::vector<std::string> positionals() const {
        static const std::vector<std::string> valueOptions = {
            "--threads", "--min-size", "--keep", "--quarantine",
        };
        std::vector<std::string> result;
        for (size_t i = 0; i < raw_.size(); ++i) {
            if (raw_[i].rfind("--", 0) == 0) {
                if (std::find(valueOptions.begin(), valueOptions.end(), raw_[i]) != valueOptions.end()) {
                    ++i; // skip this option's value
                }
                continue;
            }
            result.push_back(raw_[i]);
        }
        return result;
    }

private:
    std::vector<std::string> raw_;
};

size_t resolveThreadCount(const ArgParser& args) {
    if (auto t = args.option("--threads")) {
        int n = std::stoi(*t);
        if (n < 1) throw std::runtime_error("--threads must be >= 1");
        return static_cast<size_t>(n);
    }
    return std::max(1u, std::thread::hardware_concurrency());
}

// ---------------------------------------------------------------------
// Subcommands
// ---------------------------------------------------------------------

void printHelp() {
    std::cout <<
        R"(FileWarden -- intelligent duplicate detection & incremental backup

Usage:
  filewarden scan <path>
  filewarden dedupe <path> [--threads N] [--min-size BYTES] [--keep oldest|newest|first-path] [--quarantine <dir>] [--json]
  filewarden backup <source> <backup-root> [--threads N] [--full-rehash] [--compress] [--json]
  filewarden restore <backup-root> <snapshot-id> <destination> [--verify]
  filewarden list-snapshots <backup-root> [--json]
  filewarden --help

Examples:
  filewarden dedupe ~/Downloads --quarantine ~/Downloads/_duplicates
  filewarden backup ~/Photos ~/Backups/photos --compress
  filewarden restore ~/Backups/photos 20260802_143000 ~/Restored --verify
  filewarden list-snapshots ~/Backups/photos --json
)";
}

int cmdScan(const ArgParser& args) {
    auto pos = args.positionals();
    if (pos.empty()) {
        std::cerr << "Usage: filewarden scan <path>\n";
        return 1;
    }
    fs::path root = pos[0];

    std::cout << bold("Scanning ") << root << " ...\n";
    auto files = FileScanner::scan(root);

    std::uintmax_t totalSize = 0;
    for (auto& f : files) totalSize += f.size;

    std::cout << "\n" << bold("Scan report") << "\n";
    std::cout << "  Files:       " << files.size() << "\n";
    std::cout << "  Total size:  " << humanBytes(totalSize) << "\n";
    return 0;
}

int cmdDedupe(const ArgParser& args) {
    auto pos = args.positionals();
    if (pos.empty()) {
        std::cerr << "Usage: filewarden dedupe <path> [--threads N] [--min-size BYTES] "
                     "[--keep oldest|newest|first-path] [--quarantine <dir>] [--json]\n";
        return 1;
    }
    fs::path root = pos[0];
    size_t threads = resolveThreadCount(args);
    std::string keepStrategy = args.option("--keep").value_or("oldest");
    bool jsonMode = args.flag("--json");

    DuplicateFinderOptions opts;
    if (auto ms = args.option("--min-size")) opts.minSize = std::stoull(*ms);

    if (!jsonMode) std::cout << bold("Scanning ") << root << " ...\n";
    auto files = FileScanner::scan(root);
    if (!jsonMode) std::cout << "Found " << files.size() << " file(s). Hashing with " << threads << " thread(s)...\n\n";

    ThreadPool pool(threads);
    auto groups = DuplicateFinder::find(files, pool, opts);

    std::uintmax_t totalWasted = 0;
    for (auto& g : groups) totalWasted += g.wastedBytes();

    if (jsonMode) {
        std::ostringstream json;
        json << "{\n";
        json << "  \"root\": " << jsonEscape(root) << ",\n";
        json << "  \"filesScanned\": " << files.size() << ",\n";
        json << "  \"duplicateGroups\": [\n";
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            auto& g = groups[gi];
            fs::path keepPath = chooseKeepPath(g.files, keepStrategy);
            json << "    {\n";
            json << "      \"hash\": " << jsonEscape(g.hash) << ",\n";
            json << "      \"fileSize\": " << g.fileSize << ",\n";
            json << "      \"wastedBytes\": " << g.wastedBytes() << ",\n";
            json << "      \"keep\": " << jsonEscape(keepPath) << ",\n";
            json << "      \"files\": [";
            for (size_t fi = 0; fi < g.files.size(); ++fi) {
                json << jsonEscape(g.files[fi]);
                if (fi + 1 < g.files.size()) json << ", ";
            }
            json << "]\n";
            json << "    }" << (gi + 1 < groups.size() ? "," : "") << "\n";
        }
        json << "  ],\n";
        json << "  \"totalReclaimableBytes\": " << totalWasted << "\n";
        json << "}\n";
        std::cout << json.str();
        return 0;
    }

    if (groups.empty()) {
        std::cout << green("No duplicates found -- this directory is squeaky clean.") << "\n";
        return 0;
    }

    std::cout << bold(std::to_string(groups.size()) + " duplicate group(s) found")
               << " -- " << yellow(humanBytes(totalWasted) + " reclaimable") << "\n\n";

    for (auto& g : groups) {
        fs::path keepPath = chooseKeepPath(g.files, keepStrategy);
        std::cout << cyan("[" + g.hash.substr(0, 12) + "...]") << " " << humanBytes(g.fileSize)
                   << " x " << g.files.size() << " copies\n";
        for (auto& p : g.files) {
            bool isKept = (p == keepPath);
            std::cout << "    " << (isKept ? green("[KEEP] ") : dim("[DUPE] ")) << p.string() << "\n";
        }
    }

    if (auto qDir = args.option("--quarantine")) {
        fs::path quarantineDir = *qDir;
        std::cout << "\n" << bold("Moving duplicates to quarantine: ") << quarantineDir << "\n";
        size_t moved = 0;
        for (auto& g : groups) {
            fs::path keepPath = chooseKeepPath(g.files, keepStrategy);
            for (auto& p : g.files) {
                if (p == keepPath) continue;
                fs::path rel = fs::relative(p, root);
                moveFile(p, quarantineDir / rel);
                ++moved;
            }
        }
        std::cout << green("Moved " + std::to_string(moved) + " file(s) to quarantine.") << "\n";
        std::cout << dim("(nothing was deleted -- review the quarantine folder and remove it yourself when ready)") << "\n";
    } else {
        std::cout << "\n" << dim("(dry run -- pass --quarantine <dir> to move duplicates there)") << "\n";
    }

    return 0;
}

int cmdBackup(const ArgParser& args) {
    auto pos = args.positionals();
    if (pos.size() < 2) {
        std::cerr << "Usage: filewarden backup <source> <backup-root> [--threads N] [--full-rehash] [--compress] [--json]\n";
        return 1;
    }
    fs::path source = pos[0];
    fs::path backupRoot = pos[1];
    size_t threads = resolveThreadCount(args);
    bool jsonMode = args.flag("--json");

    BackupEngineOptions opts;
    opts.trustMTime = !args.flag("--full-rehash");
    opts.compressObjects = args.flag("--compress");

    BackupEngine engine(backupRoot, opts);
    ThreadPool pool(threads);

    if (!jsonMode) std::cout << bold("Creating snapshot: ") << source << " -> " << backupRoot << "\n";
    auto stats = engine.createSnapshot(source, pool);

    if (jsonMode) {
        std::ostringstream json;
        json << "{\n";
        json << "  \"snapshotId\": " << jsonEscape(stats.snapshotId) << ",\n";
        json << "  \"totalFiles\": " << stats.totalFiles << ",\n";
        json << "  \"newFiles\": " << stats.newFiles << ",\n";
        json << "  \"changedFiles\": " << stats.changedFiles << ",\n";
        json << "  \"unchangedFiles\": " << stats.unchangedFiles << ",\n";
        json << "  \"rehashSkipped\": " << stats.rehashSkipped << ",\n";
        json << "  \"objectsCompressed\": " << stats.objectsCompressed << ",\n";
        json << "  \"logicalSize\": " << stats.logicalSize << ",\n";
        json << "  \"bytesWritten\": " << stats.bytesWritten << "\n";
        json << "}\n";
        std::cout << json.str();
        return 0;
    }

    std::cout << "\n" << green("Snapshot " + stats.snapshotId + " created.") << "\n";
    std::cout << "  Files scanned:      " << stats.totalFiles << "\n";
    std::cout << "  New files:          " << stats.newFiles << "\n";
    std::cout << "  Changed files:      " << stats.changedFiles << "\n";
    std::cout << "  Unchanged files:    " << stats.unchangedFiles
               << " (" << stats.rehashSkipped << " skipped re-hashing via trusted mtime)\n";
    if (opts.compressObjects) {
        std::cout << "  Objects compressed: " << stats.objectsCompressed << "\n";
    }
    std::cout << "  Logical size:       " << humanBytes(stats.logicalSize) << "\n";
    std::cout << "  New bytes written:  " << humanBytes(stats.bytesWritten);
    if (stats.logicalSize > 0) {
        std::uintmax_t saved = stats.logicalSize - stats.bytesWritten;
        std::cout << "  " << dim("(" + humanBytes(saved) + " saved by reuse/dedup/compression)");
    }
    std::cout << "\n";
    return 0;
}

int cmdRestore(const ArgParser& args) {
    auto pos = args.positionals();
    if (pos.size() < 3) {
        std::cerr << "Usage: filewarden restore <backup-root> <snapshot-id> <destination> [--verify]\n";
        return 1;
    }
    fs::path backupRoot = pos[0];
    std::string snapshotId = pos[1];
    fs::path destination = pos[2];
    bool verify = args.flag("--verify");

    BackupEngine engine(backupRoot);
    std::cout << bold("Restoring snapshot " + snapshotId + " -> ") << destination;
    if (verify) std::cout << " " << dim("(with integrity verification)");
    std::cout << "\n";

    engine.restoreSnapshot(snapshotId, destination, verify);
    std::cout << green("Restore complete.") << "\n";
    return 0;
}

int cmdListSnapshots(const ArgParser& args) {
    auto pos = args.positionals();
    if (pos.empty()) {
        std::cerr << "Usage: filewarden list-snapshots <backup-root> [--json]\n";
        return 1;
    }
    fs::path backupRoot = pos[0];
    bool jsonMode = args.flag("--json");
    BackupEngine engine(backupRoot);
    auto ids = engine.listSnapshots();

    if (jsonMode) {
        std::ostringstream json;
        json << "{\n";
        json << "  \"backupRoot\": " << jsonEscape(backupRoot) << ",\n";
        json << "  \"snapshots\": [\n";
        for (size_t i = 0; i < ids.size(); ++i) {
            Manifest m = Manifest::load(backupRoot / "manifests" / (ids[i] + ".manifest"));
            std::uintmax_t total = 0;
            for (auto& e : m.entries) total += e.size;
            json << "    {\"id\": " << jsonEscape(ids[i]) << ", \"files\": " << m.entries.size()
                 << ", \"totalSize\": " << total << "}" << (i + 1 < ids.size() ? "," : "") << "\n";
        }
        json << "  ]\n";
        json << "}\n";
        std::cout << json.str();
        return 0;
    }

    if (ids.empty()) {
        std::cout << "No snapshots found in " << backupRoot << "\n";
        return 0;
    }

    std::cout << bold(std::to_string(ids.size()) + " snapshot(s) in " + backupRoot.string() + ":") << "\n";
    for (auto& id : ids) {
        Manifest m = Manifest::load(backupRoot / "manifests" / (id + ".manifest"));
        std::uintmax_t total = 0;
        for (auto& e : m.entries) total += e.size;
        std::cout << "  " << cyan(id) << "  (" << m.entries.size() << " files, " << humanBytes(total) << ")\n";
    }
    return 0;
}

} // namespace

int runCli(int argc, char** argv) {
    std::vector<std::string> all(argv + 1, argv + argc);
    if (all.empty()) {
        printHelp();
        return 1;
    }

    std::string cmd = all[0];
    ArgParser args(std::vector<std::string>(all.begin() + 1, all.end()));

    try {
        if (cmd == "scan") return cmdScan(args);
        if (cmd == "dedupe") return cmdDedupe(args);
        if (cmd == "backup") return cmdBackup(args);
        if (cmd == "restore") return cmdRestore(args);
        if (cmd == "list-snapshots") return cmdListSnapshots(args);
        if (cmd == "--help" || cmd == "-h" || cmd == "help") {
            printHelp();
            return 0;
        }

        std::cerr << "Unknown command: " << cmd << "\n\n";
        printHelp();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << yellow("Error: ") << e.what() << "\n";
        return 1;
    }
}

} // namespace filewarden
