#include "test_framework.hpp"
#include "filewarden/sha256.hpp"

#include <fstream>

using namespace filewarden;

TEST(sha256_empty_string) {
    CHECK_EQ(Sha256::hash(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST(sha256_abc) {
    CHECK_EQ(Sha256::hash("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

TEST(sha256_quick_brown_fox) {
    CHECK_EQ(Sha256::hash("The quick brown fox jumps over the lazy dog"),
             std::string("d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"));
}

TEST(sha256_multi_block_message) {
    // 200 bytes spans more than three 64-byte blocks once padding is added,
    // exercising the multi-block code path (not just the single-block one).
    std::string s(200, 'x');
    CHECK_EQ(Sha256::hash(s), std::string("aa20c23e3201834050679e1d88941b9a6fed0557c9a705cb2c315e2e63fd486d"));
}

TEST(sha256_same_input_same_output) {
    CHECK_EQ(Sha256::hash("filewarden"), Sha256::hash("filewarden"));
}

TEST(sha256_different_input_different_output) {
    CHECK_TRUE(Sha256::hash("filewarden") != Sha256::hash("filewarden!"));
}

TEST(sha256_streaming_update_matches_one_shot) {
    Sha256 streamed;
    streamed.update(std::string("hello "));
    streamed.update(std::string("world"));
    CHECK_EQ(streamed.finalizeHex(), Sha256::hash("hello world"));
}

TEST(sha256_hash_file_matches_hash_of_contents) {
    std::string path = "/tmp/filewarden_test_sha_file.txt";
    {
        std::ofstream f(path, std::ios::binary);
        f << "content for file hashing test";
    }
    CHECK_EQ(Sha256::hashFile(path), Sha256::hash("content for file hashing test"));
    std::remove(path.c_str());
}

TEST(sha256_partial_hash_only_covers_prefix) {
    std::string path = "/tmp/filewarden_test_sha_partial.txt";
    {
        std::ofstream f(path, std::ios::binary);
        f << "0123456789ABCDEF";
    }
    // First 4 bytes hashed on their own should match a partial hash over the file.
    CHECK_EQ(Sha256::hashFile(path, 4), Sha256::hash("0123"));
    CHECK_TRUE(Sha256::hashFile(path, 4) != Sha256::hashFile(path)); // partial != full
    std::remove(path.c_str());
}

TEST(sha256_finalize_twice_throws) {
    Sha256 sha;
    sha.update(std::string("x"));
    sha.finalizeHex();
    CHECK_THROWS(sha.finalizeHex());
}
