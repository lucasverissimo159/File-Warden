#include "filewarden/duplicate_finder.hpp"
#include "filewarden/sha256.hpp"

#include <algorithm>
#include <future>
#include <unordered_map>

namespace fs = std::filesystem;

namespace filewarden {

std::vector<DuplicateGroup> DuplicateFinder::find(const std::vector<FileEntry>& files,
                                                    ThreadPool& pool,
                                                    const DuplicateFinderOptions& options) {
    // Stage 1: bucket by size.
    std::unordered_map<std::uintmax_t, std::vector<fs::path>> bySize;
    for (const auto& f : files) {
        if (f.size < options.minSize) continue;
        bySize[f.size].push_back(f.absolutePath);
    }

    std::vector<DuplicateGroup> result;

    for (auto& [size, paths] : bySize) {
        if (paths.size() < 2) continue; // unique size -> cannot be a duplicate

        // If the entire file fits inside the partial-hash window, the
        // partial hash already covers 100% of the content, so there is no
        // point re-reading it for a "full" hash in stage 3.
        const bool wholeFileFits = size <= static_cast<std::uintmax_t>(options.partialHashBytes);
        const std::uintmax_t hashBytes = wholeFileFits ? size : static_cast<std::uintmax_t>(options.partialHashBytes);

        // Stage 2: cheap partial hash, in parallel.
        std::vector<std::future<std::string>> partialFutures;
        partialFutures.reserve(paths.size());
        for (const auto& p : paths) {
            partialFutures.push_back(pool.enqueue([p, hashBytes]() { return Sha256::hashFile(p, hashBytes); }));
        }

        std::unordered_map<std::string, std::vector<fs::path>> byPartial;
        for (size_t i = 0; i < paths.size(); ++i) {
            byPartial[partialFutures[i].get()].push_back(paths[i]);
        }

        for (auto& [partialHash, candidates] : byPartial) {
            if (candidates.size() < 2) continue; // partial hash was unique -> not a duplicate

            if (wholeFileFits) {
                std::sort(candidates.begin(), candidates.end());
                result.push_back(DuplicateGroup{partialHash, size, std::move(candidates)});
                continue;
            }

            // Stage 3: confirm with a full-content hash, in parallel. The
            // partial hash only proves the first N bytes match; two large
            // files can easily share the same opening bytes (a common file
            // header, for instance) while differing later on.
            std::vector<std::future<std::string>> fullFutures;
            fullFutures.reserve(candidates.size());
            for (const auto& p : candidates) {
                fullFutures.push_back(pool.enqueue([p]() { return Sha256::hashFile(p); }));
            }

            std::unordered_map<std::string, std::vector<fs::path>> byFull;
            for (size_t i = 0; i < candidates.size(); ++i) {
                byFull[fullFutures[i].get()].push_back(candidates[i]);
            }

            for (auto& [fullHash, confirmed] : byFull) {
                if (confirmed.size() < 2) continue;
                std::sort(confirmed.begin(), confirmed.end());
                result.push_back(DuplicateGroup{fullHash, size, std::move(confirmed)});
            }
        }
    }

    std::sort(result.begin(), result.end(), [](const DuplicateGroup& a, const DuplicateGroup& b) {
        return a.wastedBytes() > b.wastedBytes();
    });

    return result;
}

} // namespace filewarden
