#include "filewarden/manifest.hpp"

#include <cstdio>
#include <cstdlib>
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
            char c = static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            out += c;
            i += 2;
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
        if (!std::getline(iss, hash, '\t')) continue;
        if (!std::getline(iss, sizeStr, '\t')) continue;
        if (!std::getline(iss, mtimeStr, '\t')) continue;
        if (!std::getline(iss, pathStr)) continue;

        ManifestEntry e;
        e.hash = hash;
        e.size = std::stoull(sizeStr);
        e.mtimeEpochSeconds = std::stoll(mtimeStr);
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
