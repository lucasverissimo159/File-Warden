#include "filewarden/manifest.hpp"

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace filewarden {

namespace {

// Percent-escapes tabs, newlines and '%' itself so a path can never be
// confused with the tab-separated record structure, however unusual the
// filename. Rare in practice, but a backup tool silently mangling a path is
// exactly the kind of "rare" bug that erodes trust the one time it happens.
std::string escapePath(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '%' || c == '\t' || c == '\n' || c == '\r') {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string unescapePath(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            if (!std::isxdigit(static_cast<unsigned char>(s[i + 1])) ||
                !std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
                throw std::runtime_error("Invalid path escape in manifest");
            }
            char c = static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            out += c;
            i += 2;
        } else if (s[i] == '%') {
            throw std::runtime_error("Truncated path escape in manifest");
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace

void Manifest::save(const fs::path& file) const {
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot write manifest: " + file.string());
    }
    out << "# filewarden manifest v1\n";
    out << "# hash\tsize\tmtime_epoch\trelative_path\n";
    for (const auto& e : entries) {
        out << e.hash << '\t' << e.size << '\t' << e.mtimeEpochSeconds << '\t'
            << escapePath(e.relativePath.generic_string()) << '\n';
    }
    if (!out) {
        throw std::runtime_error("Error while writing manifest: " + file.string());
    }
}

Manifest Manifest::load(const fs::path& file) {
    Manifest m;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot read manifest: " + file.string());
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string hash, sizeStr, mtimeStr, pathStr;
        if (!std::getline(iss, hash, '\t') || !std::getline(iss, sizeStr, '\t') ||
            !std::getline(iss, mtimeStr, '\t') || !std::getline(iss, pathStr) || pathStr.empty()) {
            throw std::runtime_error("Malformed manifest entry in " + file.string());
        }

        ManifestEntry e;
        e.hash = hash;
        try {
            e.size = std::stoull(sizeStr);
            e.mtimeEpochSeconds = std::stoll(mtimeStr);
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid numeric field in manifest " + file.string());
        }
        e.relativePath = unescapePath(pathStr);
        m.entries.push_back(std::move(e));
    }
    return m;
}

std::unordered_map<std::string, ManifestEntry> Manifest::toIndex() const {
    std::unordered_map<std::string, ManifestEntry> idx;
    idx.reserve(entries.size());
    for (const auto& e : entries) {
        idx[e.relativePath.generic_string()] = e;
    }
    return idx;
}

} // namespace filewarden
