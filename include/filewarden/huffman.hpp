#pragma once

#include <string>

namespace filewarden {

// A from-scratch canonical Huffman coder used to optionally compress objects
// in BackupEngine's content store.
//
// "Canonical" means the compressed output stores only the *length* of each
// symbol's code (256 bytes, one per possible byte value), not the codes
// themselves or the tree structure -- both the encoder and decoder
// deterministically reconstruct the same bit patterns from those lengths
// alone. This is the same trick DEFLATE (the algorithm behind gzip and ZIP)
// uses to keep the header compact. It trades a small, fixed 256-byte header
// overhead for a format that's simple to get exactly right, which matters
// for a backup tool: an encoder that's merely *usually* correct is worse
// than no compression at all.
//
// This operates on whole in-memory buffers rather than streaming, unlike
// Sha256 -- a deliberate scope tradeoff. Streaming Huffman coding is
// possible but meaningfully more complex (the code table can't be finalized
// until the whole input has been scanned for frequencies), and BackupEngine
// only calls this on individual files rather than unbounded streams.
class Huffman {
public:
    // Compresses `input` into a self-contained buffer: everything needed to
    // reverse the operation (the code-length table and the original byte
    // count) is embedded in the output, so decompress(compress(x)) == x
    // always holds with no external state.
    static std::string compress(const std::string& input);
    static std::string decompress(const std::string& compressed);
};

} // namespace filewarden
