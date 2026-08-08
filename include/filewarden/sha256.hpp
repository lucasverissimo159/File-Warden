#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace filewarden {

// A from-scratch, dependency-free implementation of SHA-256 (FIPS 180-4).
//
// FileWarden uses SHA-256 purely as a *content identifier*: two files with the
// same hash are treated as having identical content, the same idea Git uses
// for its object store. This is not used for any security/authentication
// purpose, so there is no key material, no secrets, nothing to keep private
// about the algorithm itself -- it's a well-known public standard.
//
// The class exposes a streaming interface (update() can be called repeatedly)
// so files can be hashed in fixed-size chunks without ever loading the whole
// file into memory, which matters once you point this at a folder full of
// multi-gigabyte video files.
class Sha256 {
public:
    Sha256();

    void update(const uint8_t* data, size_t len);
    void update(const std::string& data);

    // Finalizes the hash and returns it as a 64-character lowercase hex
    // string. Must be called exactly once; further update() calls after this
    // are not supported.
    std::string finalizeHex();

    // Convenience one-shot helper for an in-memory buffer.
    static std::string hash(const std::string& data);

    // Convenience helper for hashing a file's contents, streaming it through
    // a fixed-size buffer rather than reading it all into memory at once.
    //
    // If `maxBytes` is non-zero, only the first `maxBytes` bytes of the file
    // are hashed. DuplicateFinder uses this to compute a cheap "partial hash"
    // over just the first few KB of a file, to cheaply rule out non-matches
    // before paying the cost of reading (and hashing) an entire large file.
    static std::string hashFile(const std::filesystem::path& path,
                                 std::uintmax_t maxBytes = 0,
                                 size_t bufferSize = 1 << 16);

private:
    void appendRaw(const uint8_t* data, size_t len);
    void processBlock(const uint8_t* block);

    std::array<uint32_t, 8> h_{};
    std::array<uint8_t, 64> buffer_{};
    size_t bufferLen_ = 0;
    uint64_t totalLen_ = 0; // total bytes fed via update(), used for the length suffix
    bool finalized_ = false;
};

} // namespace filewarden
