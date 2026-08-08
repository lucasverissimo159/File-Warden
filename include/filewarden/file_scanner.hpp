#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace filewarden {

struct FileEntry {
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath; // relative to the scan root
    std::uintmax_t size = 0;
    std::filesystem::file_time_type lastWriteTime{};
};

struct FileScannerOptions {
    bool followSymlinks = false;
};

class FileScanner {
public:
    // Recursively walks `root` and returns metadata for every regular file
    // found. Directories or files that can't be accessed (permission errors,
    // broken symlinks, etc.) are skipped with a warning on stderr rather than
    // aborting the whole scan -- a real filesystem always has a few of these,
    // and a duplicate finder that crashes on the first locked file isn't
    // useful to anyone.
    static std::vector<FileEntry> scan(const std::filesystem::path& root, const FileScannerOptions& options = {});
};

} // namespace filewarden
