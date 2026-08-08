#include "test_framework.hpp"
#include "filewarden/huffman.hpp"

#include <random>
#include <string>

using namespace filewarden;

namespace {
bool roundTrips(const std::string& input) {
    return Huffman::decompress(Huffman::compress(input)) == input;
}
} // namespace

TEST(huffman_empty_input_round_trips) {
    CHECK_TRUE(roundTrips(""));
}

TEST(huffman_single_character_round_trips) {
    CHECK_TRUE(roundTrips("a"));
}

TEST(huffman_single_character_repeated_round_trips) {
    CHECK_TRUE(roundTrips(std::string(500, 'z')));
}

TEST(huffman_two_distinct_symbols_round_trip) {
    CHECK_TRUE(roundTrips("ab"));
    CHECK_TRUE(roundTrips(std::string(300, 'a') + std::string(300, 'b')));
}

TEST(huffman_all_256_byte_values_round_trip) {
    std::string s;
    for (int i = 0; i < 256; ++i) s += static_cast<char>(i);
    CHECK_TRUE(roundTrips(s));
}

TEST(huffman_english_like_text_round_trips_and_shrinks) {
    // Needs to be comfortably larger than the fixed 264-byte header (256
    // code lengths + 8-byte length field) for a shrink to be a realistic
    // expectation -- a short snippet correctly *grows* under this format,
    // which is exactly why BackupEngine compares sizes and falls back to
    // raw storage rather than assuming compression always helps.
    std::string paragraph =
        "the quick brown fox jumps over the lazy dog. the dog barks at the fox. "
        "the quick fox runs away while the dog watches from the tall grass. ";
    std::string s;
    for (int i = 0; i < 20; ++i) s += paragraph; // ~2.9 KB, well past the header cost

    std::string compressed = Huffman::compress(s);
    CHECK_TRUE(Huffman::decompress(compressed) == s);
    CHECK_TRUE(compressed.size() < s.size());
}

TEST(huffman_short_text_may_legitimately_grow_due_to_header_overhead) {
    // Documents the tradeoff explicitly as a test, rather than leaving it as
    // an implicit surprise: a short input's compressed form can be *larger*
    // than the original because of the fixed 264-byte header. Correctness
    // (round-tripping) must still hold even when compression doesn't pay off.
    std::string s = "short";
    std::string compressed = Huffman::compress(s);
    CHECK_TRUE(Huffman::decompress(compressed) == s);
    CHECK_TRUE(compressed.size() > s.size());
}

TEST(huffman_repetitive_data_compresses_significantly) {
    std::string s;
    for (int i = 0; i < 5000; ++i) s += "ABCABCABCXYZ";
    std::string compressed = Huffman::compress(s);
    CHECK_TRUE(Huffman::decompress(compressed) == s);
    CHECK_TRUE(compressed.size() < s.size() / 2); // should shrink substantially
}

TEST(huffman_pseudo_random_data_still_round_trips) {
    std::mt19937 rng(1234);
    std::string s(3000, '\0');
    for (auto& c : s) c = static_cast<char>(rng() & 0xFF);
    CHECK_TRUE(roundTrips(s)); // correctness must hold even when compression can't help
}

TEST(huffman_binary_data_with_null_bytes_round_trips) {
    std::string s;
    for (int i = 0; i < 1000; ++i) s += static_cast<char>((i * 37) % 256);
    CHECK_TRUE(roundTrips(s));
}

TEST(huffman_compress_is_deterministic_for_same_input) {
    std::string s = "deterministic output please";
    CHECK_TRUE(Huffman::compress(s) == Huffman::compress(s));
}

TEST(huffman_decompress_rejects_truncated_stream) {
    CHECK_THROWS(Huffman::decompress("too short"));
}
