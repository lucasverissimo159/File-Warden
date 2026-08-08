#include "filewarden/file_scanner.hpp"

#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace filewarden {

std::vector<FileEntry> FileScanner::scan(const fs::path& root, const FileScannerOptions& options) {
    std::vector<FileEntry> results;

    std::error_code existsEc;
    if (!fs::exists(root, existsEc)) {
        throw std::runtime_error("Path does not exist: " + root.string());
    }

    auto dirOptions = options.followSymlinks
                           ? fs::directory_options::follow_directory_symlink
                           : fs::directory_options::none;

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, dirOptions, ec);
         it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            std::cerr << "Warning: cannot access " << it->path() << ": " << ec.message() << "\n";
            ec.clear();
            continue;
        }

        const auto& dirEntry = *it;

        std::error_code typeEc;
        bool isRegular = dirEntry.is_regular_file(typeEc);
        if (typeEc || !isRegular) {
            continue;
        }

        FileEntry entry;
        entry.absolutePath = dirEntry.path();

        std::error_code relEc;
        entry.relativePath = fs::relative(dirEntry.path(), root, relEc);
        if (relEc || entry.relativePath.empty()) {
            entry.relativePath = dirEntry.path().filename();
        }

        std::error_code sizeEc;
        entry.size = dirEntry.file_size(sizeEc);
        if (sizeEc) {
            std::cerr << "Warning: cannot stat " << dirEntry.path() << ": " << sizeEc.message() << "\n";
            continue;
        }

        std::error_code timeEc;
        entry.lastWriteTime = dirEntry.last_write_time(timeEc);

        results.push_back(std::move(entry));
    }

    if (ec) {
        std::cerr << "Warning: scan of " << root << " ended early: " << ec.message() << "\n";
    }

    return results;
}

} // namespace filewarden
