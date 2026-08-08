#include "filewarden/sha256.hpp"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace filewarden {

namespace {

// Round constants: the first 32 bits of the fractional parts of the cube
// roots of the first 64 prime numbers (FIPS 180-4, section 4.2.2).
constexpr std::array<uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

} // namespace

Sha256::Sha256() {
    // Initial hash values: the first 32 bits of the fractional parts of the
    // square roots of the first 8 prime numbers (FIPS 180-4, section 5.3.3).
    h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
}

void Sha256::appendRaw(const uint8_t* data, size_t len) {
    while (len > 0) {
        size_t toCopy = std::min(len, size_t(64) - bufferLen_);
        std::memcpy(buffer_.data() + bufferLen_, data, toCopy);
        bufferLen_ += toCopy;
        data += toCopy;
        len -= toCopy;
        if (bufferLen_ == 64) {
            processBlock(buffer_.data());
            bufferLen_ = 0;
        }
    }
}

void Sha256::update(const uint8_t* data, size_t len) {
    totalLen_ += len;
    appendRaw(data, len);
}

void Sha256::update(const std::string& data) {
    update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void Sha256::processBlock(const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8) | (uint32_t(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        hh = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
}

std::string Sha256::finalizeHex() {
    if (finalized_) {
        throw std::logic_error("Sha256::finalizeHex() called more than once");
    }
    finalized_ = true;

    // Capture the true bit length *before* we start appending padding bytes
    // below (appendRaw does not touch totalLen_, only update() does, but we
    // grab it first regardless to keep the intent unambiguous).
    uint64_t bitLen = totalLen_ * 8;

    const uint8_t one = 0x80;
    appendRaw(&one, 1);

    const uint8_t zero = 0x00;
    while (bufferLen_ != 56) {
        appendRaw(&zero, 1);
    }

    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) {
        lenBytes[7 - i] = static_cast<uint8_t>(bitLen >> (8 * i));
    }
    appendRaw(lenBytes, 8);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint32_t word : h_) {
        oss << std::setw(8) << word;
    }
    return oss.str();
}

std::string Sha256::hash(const std::string& data) {
    Sha256 sha;
    sha.update(data);
    return sha.finalizeHex();
}

std::string Sha256::hashFile(const std::filesystem::path& path, std::uintmax_t maxBytes, size_t bufferSize) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open file for hashing: " + path.string());
    }

    Sha256 sha;
    std::vector<uint8_t> buffer(bufferSize);
    bool limited = maxBytes > 0;
    std::uintmax_t remaining = maxBytes;

    while (in) {
        size_t toRead = bufferSize;
        if (limited) {
            if (remaining == 0) break;
            toRead = static_cast<size_t>(std::min<std::uintmax_t>(bufferSize, remaining));
        }
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(toRead));
        std::streamsize got = in.gcount();
        if (got <= 0) break;
        sha.update(buffer.data(), static_cast<size_t>(got));
        if (limited) remaining -= static_cast<std::uintmax_t>(got);
    }

    return sha.finalizeHex();
}

} // namespace filewarden
