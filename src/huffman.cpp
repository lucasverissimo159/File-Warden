#include "filewarden/huffman.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace filewarden {

namespace {

struct Node {
    uint64_t freq;
    int symbol; // -1 for internal nodes
    int left = -1;
    int right = -1;
};

// Given code lengths (0 = symbol absent), deterministically assigns the
// canonical codeword for every present symbol. Run identically by both the
// encoder (to know what bits to write) and the decoder (to know what bits to
// expect) -- this is the entire point of canonical Huffman: only the lengths
// need to travel with the data, never the codes or the tree itself.
std::vector<std::pair<int, uint32_t>> assignCanonicalCodes(const std::array<uint8_t, 256>& codeLength) {
    std::vector<int> symbols;
    for (int i = 0; i < 256; ++i) {
        if (codeLength[i] > 0) symbols.push_back(i);
    }
    std::sort(symbols.begin(), symbols.end(), [&](int a, int b) {
        if (codeLength[a] != codeLength[b]) return codeLength[a] < codeLength[b];
        return a < b;
    });

    std::vector<std::pair<int, uint32_t>> result;
    result.reserve(symbols.size());

    uint32_t currentCode = 0;
    int prevLen = 0;
    for (int s : symbols) {
        int len = codeLength[s];
        currentCode <<= (len - prevLen);
        result.push_back({s, currentCode});
        currentCode += 1;
        prevLen = len;
    }
    return result;
}

std::array<uint8_t, 256> computeCodeLengths(const std::string& input) {
    std::array<uint64_t, 256> freq{};
    for (unsigned char c : input) freq[c]++;

    std::array<uint8_t, 256> codeLength{};

    int distinct = 0, only = -1;
    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) { distinct++; only = i; }
    }

    if (distinct == 1) {
        // A single distinct byte value can't form a 2-child tree; a Huffman
        // tree needs at least two leaves. Give it a 1-bit code -- there's
        // nothing to distinguish it from, so any fixed code works, and the
        // decoder only needs to know how many times to repeat it (from the
        // original-length field), not what the "other" symbol would be.
        codeLength[only] = 1;
        return codeLength;
    }
    if (distinct == 0) {
        return codeLength; // empty input: nothing to encode
    }

    std::vector<Node> nodes;
    nodes.reserve(2 * distinct);
    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) nodes.push_back(Node{freq[i], i, -1, -1});
    }

    auto cmp = [&nodes](int a, int b) { return nodes[a].freq > nodes[b].freq; };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> heap(cmp);
    for (size_t i = 0; i < nodes.size(); ++i) heap.push(static_cast<int>(i));

    while (heap.size() > 1) {
        int a = heap.top(); heap.pop();
        int b = heap.top(); heap.pop();
        nodes.push_back(Node{nodes[a].freq + nodes[b].freq, -1, a, b});
        heap.push(static_cast<int>(nodes.size()) - 1);
    }
    int root = heap.top();

    std::vector<std::pair<int, int>> stack; // (nodeIndex, depth)
    stack.push_back({root, 0});
    while (!stack.empty()) {
        auto [idx, depth] = stack.back();
        stack.pop_back();
        const Node& n = nodes[idx];
        if (n.symbol != -1) {
            codeLength[n.symbol] = static_cast<uint8_t>(depth);
        } else {
            stack.push_back({n.left, depth + 1});
            stack.push_back({n.right, depth + 1});
        }
    }

    return codeLength;
}

} // namespace

std::string Huffman::compress(const std::string& input) {
    std::array<uint8_t, 256> codeLength = computeCodeLengths(input);
    auto codes = assignCanonicalCodes(codeLength); // (symbol, code) pairs, canonical order

    std::array<uint32_t, 256> codeOf{};
    for (auto& [symbol, code] : codes) codeOf[symbol] = code;

    std::string out;
    out.reserve(256 + 8 + input.size() / 2 + 1);

    for (int i = 0; i < 256; ++i) out += static_cast<char>(codeLength[i]);

    uint64_t originalLen = input.size();
    for (int i = 7; i >= 0; --i) out += static_cast<char>((originalLen >> (8 * i)) & 0xFF);

    uint8_t bitBuffer = 0;
    int bitsFilled = 0;
    for (unsigned char c : input) {
        uint32_t bits = codeOf[c];
        int len = codeLength[c];
        for (int b = len - 1; b >= 0; --b) {
            bitBuffer = static_cast<uint8_t>((bitBuffer << 1) | ((bits >> b) & 1u));
            if (++bitsFilled == 8) {
                out += static_cast<char>(bitBuffer);
                bitBuffer = 0;
                bitsFilled = 0;
            }
        }
    }
    if (bitsFilled > 0) {
        bitBuffer = static_cast<uint8_t>(bitBuffer << (8 - bitsFilled));
        out += static_cast<char>(bitBuffer);
    }

    return out;
}

std::string Huffman::decompress(const std::string& compressed) {
    constexpr size_t headerSize = 256 + 8;
    if (compressed.size() < headerSize) {
        throw std::runtime_error("Corrupt Huffman stream: shorter than the fixed header");
    }

    std::array<uint8_t, 256> codeLength{};
    for (int i = 0; i < 256; ++i) codeLength[i] = static_cast<uint8_t>(compressed[static_cast<size_t>(i)]);

    uint64_t originalLen = 0;
    for (int i = 0; i < 8; ++i) {
        originalLen = (originalLen << 8) | static_cast<uint8_t>(compressed[256 + static_cast<size_t>(i)]);
    }

    std::string result;
    result.reserve(originalLen);
    if (originalLen == 0) return result;

    auto codes = assignCanonicalCodes(codeLength);

    uint8_t maxLen = 0;
    for (auto& [symbol, code] : codes) maxLen = std::max(maxLen, codeLength[symbol]);

    // decodeTable[length][code] = symbol. Sized to the actual max code
    // length present rather than a fixed 256, since real files rarely need
    // anywhere near that many distinct lengths.
    std::vector<std::unordered_map<uint32_t, int>> decodeTable(static_cast<size_t>(maxLen) + 1);
    for (auto& [symbol, code] : codes) {
        decodeTable[codeLength[symbol]][code] = symbol;
    }

    size_t bitPos = headerSize * 8;
    size_t totalBits = compressed.size() * 8;

    uint32_t currentCode = 0;
    int currentLen = 0;

    while (result.size() < originalLen) {
        if (bitPos >= totalBits) {
            throw std::runtime_error("Corrupt Huffman stream: ran out of bits before reaching the expected length");
        }
        size_t byteIdx = bitPos / 8;
        int bitIdx = 7 - static_cast<int>(bitPos % 8);
        int bit = (static_cast<uint8_t>(compressed[byteIdx]) >> bitIdx) & 1;
        ++bitPos;

        currentCode = (currentCode << 1) | static_cast<uint32_t>(bit);
        ++currentLen;

        if (currentLen > maxLen) {
            throw std::runtime_error("Corrupt Huffman stream: no matching code found");
        }
        auto it = decodeTable[static_cast<size_t>(currentLen)].find(currentCode);
        if (it != decodeTable[static_cast<size_t>(currentLen)].end()) {
            result += static_cast<char>(it->second);
            currentCode = 0;
            currentLen = 0;
        }
    }

    return result;
}

} // namespace filewarden
