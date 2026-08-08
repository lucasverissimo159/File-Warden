# FileWarden

**Find duplicate files. Back them up incrementally. Trust that both actually worked.**

[![CI](https://github.com/YOUR_USERNAME/filewarden/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_USERNAME/filewarden/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Zero dependencies](https://img.shields.io/badge/dependencies-zero-brightgreen.svg)

> Replace `YOUR_USERNAME` above once this is pushed to GitHub, and the badge will reflect real CI runs.

## The problem

Every Downloads folder eventually looks like this: the same photo saved three
times because you weren't sure which folder it belonged in, a report you
"backed up" by copying it next to itself, a screenshot and its `(1)` copy.
Meanwhile, the actual backup you meant to set up either doesn't exist, re-copies
everything from scratch every time (slow, and wasteful of disk space), or you
have no real way to know if last night's run actually worked.

FileWarden is a single small command-line tool that solves both problems using
the same underlying idea: **identify files by what they contain, not by their
name or location.**

## Features

- **`dedupe`** — finds files with byte-for-byte identical content anywhere
  under a directory, no matter what they're named or how deep they're nested.
- **`backup`** — creates space-efficient, incremental snapshots. Unchanged
  files cost zero extra disk space. Identical content is only ever stored
  once, even across different files and different snapshots. Optional
  `--compress` shrinks new objects with a built-in Huffman coder.
- **`restore`** — brings any past snapshot back, with optional cryptographic
  integrity verification.
- `--json` on `dedupe`, `backup` and `list-snapshots` for scripting or piping
  into other tools.
- Safety-first by design: `dedupe` never deletes anything. It reports what it
  found and, if asked, moves duplicates into a quarantine folder you can
  review and delete yourself.
- Zero external dependencies. No package manager, no vendored libraries, no
  network access required to build — just a C++17 compiler and CMake.

## Why this isn't just "for loop + delete"

Anyone can write a script that hashes every file in a folder. The interesting
part — and the part that mirrors how production tools like Git, Time Machine,
restic and Borg actually work — is in four design decisions:

**1. A three-stage filter before any expensive hashing happens.**
Comparing file *content* is expensive; comparing file *size* is nearly free.
`dedupe` groups files by size first (files of different sizes can never be
duplicates), then hashes only the first 4&nbsp;KB of the survivors to cheaply
rule out non-matches, and only computes a full SHA-256 over files that survive
both filters. On a typical messy folder, this means full file reads happen for
a small fraction of what's on disk.

**2. Content-addressable storage with hard links for backups.**
`backup` doesn't copy files into timestamped folders the naive way. Every
unique piece of content is written once into an object store, named by its own
SHA-256 hash (`objects/ab/ab3f...`, sharded the way Git shards its object
database). Each snapshot is then a directory of **hard links** pointing into
that store. A file that hasn't changed since last time costs zero additional
bytes on disk — it's the same inode, just linked from a new place. A file
whose content happens to match some *other* file (a copy-pasted photo, a
duplicated report) is also only ever stored once, automatically, as a side
effect of the same mechanism.

**3. A trust-but-verify incremental strategy, with an explicit escape hatch.**
Re-hashing every file on every backup run defeats the point of "incremental."
By default, `backup` trusts a file's previous hash if its size and
modification time exactly match what was recorded last time (the same
optimization `rsync` uses) — so a second backup of a mostly-unchanged tree
only pays the cost of reading what's actually new. Because this is a
heuristic and not a proof, `--full-rehash` is there for anyone who wants
byte-for-byte certainty instead of speed.

**4. Compression that measures instead of assuming.**
`--compress` runs new objects through a from-scratch canonical Huffman
coder before they're written to the store. But Huffman coding only exploits
skewed *byte frequency* — it does nothing for repeated sequences the way
LZ77/DEFLATE does, and it carries a fixed 264-byte header. That means a short
file, or one that's already compressed (a JPEG, an MP4), can come out
*larger*. So FileWarden never assumes: it compresses into memory, compares
the result against the original size, and keeps whichever is smaller,
per file. Compression can only help or be a no-op — never hurt — which is
the only property worth having for something touching your backups.

None of this exists to look clever — each piece is there because the naive
version of that piece (hash everything always; copy everything every time;
never re-verify; compress blindly) is either too slow, too fragile, or too
wasteful to trust with your actual files.

## Architecture

```
                     ┌─────────────────┐
                     │   FileScanner    │   recursive directory walk
                     └────────┬─────────┘
                              │ FileEntry[] (path, size, mtime)
                              ▼
                     ┌─────────────────┐
                     │    ThreadPool    │   spreads hashing across cores
                     └────────┬─────────┘
                              │
              ┌───────────────┴────────────────┐
              ▼                                 ▼
   ┌───────────────────────┐       ┌──────────────────────────┐
   │    DuplicateFinder      │       │       BackupEngine         │
   │  size → partial hash    │       │  content-addressable store  │
   │       → full hash       │       │  + hard-linked snapshots    │
   └───────────┬─────────────┘       └─────────────┬──────────────┘
               │                                    │  optional --compress
               │                                    ▼
               │                          ┌───────────────────┐
               │                          │      Huffman        │
               │                          │  canonical coder,   │
               │                          │  smaller-of-two     │
               │                          └──────────┬──────────┘
               ▼                                     ▼
      duplicate groups                      Manifest (per snapshot)
      (report / quarantine)               restore • list-snapshots
```

Every hash in the system, from the 4&nbsp;KB pre-filter to the full-file
digest, is computed by a from-scratch, dependency-free `Sha256` class
(`include/filewarden/sha256.hpp`) with a streaming interface, so a
multi-gigabyte file is hashed through a fixed-size buffer instead of being
loaded into memory whole. Compression (`include/filewarden/huffman.hpp`) is
the one component that isn't streaming — it needs a file's full contents in
memory to compute byte frequencies before it can encode anything — which is
a deliberate, documented scope tradeoff rather than an oversight.

## Getting started

```bash
git clone https://github.com/YOUR_USERNAME/filewarden.git
cd filewarden
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

This produces two binaries: `build/filewarden` (the tool) and
`build/filewarden_tests` (the test suite).

Try it immediately with the included demo data:

```bash
bash examples/generate_sample_data.sh
./build/filewarden dedupe examples/sample_data
```

## Usage

```
filewarden scan <path>
filewarden dedupe <path> [--threads N] [--min-size BYTES] [--keep oldest|newest|first-path] [--quarantine <dir>] [--json]
filewarden backup <source> <backup-root> [--threads N] [--full-rehash] [--compress] [--json]
filewarden restore <backup-root> <snapshot-id> <destination> [--verify]
filewarden list-snapshots <backup-root> [--json]
```

### `dedupe` — real output from `examples/generate_sample_data.sh`

```
$ filewarden dedupe examples/sample_data --threads 4
Scanning "examples/sample_data" ...
Found 9 file(s). Hashing with 4 thread(s)...

2 duplicate group(s) found -- 236 B reclaimable

[d400842b76d0...] 63 B x 3 copies
    [DUPE] examples/sample_data/Documents/Reports/backup_someone_made_by_hand/q3_report.docx
    [KEEP] examples/sample_data/Documents/Reports/q3_report.docx
    [DUPE] examples/sample_data/Downloads/q3_report_FINAL_v2.docx
[a31447885cf0...] 55 B x 3 copies
    [DUPE] examples/sample_data/Downloads/sunset (1).jpg
    [DUPE] examples/sample_data/Photos/2024/Vacation/edited_copy/sunset.jpg
    [KEEP] examples/sample_data/Photos/2024/Vacation/sunset.jpg

(dry run -- pass --quarantine <dir> to move duplicates there)
```

Two files with the exact same size but different content (`token_a.txt`,
`token_b.txt`, both 10 bytes) are correctly **not** reported — proving the
partial-hash stage isn't just trusting file size.

`--json` produces the same result as real, valid JSON (piped through
Python's `json` module below just to prove it parses):

```
$ filewarden dedupe examples/sample_data --json | python3 -m json.tool | head -8
{
    "root": "examples/sample_data",
    "filesScanned": 9,
    "duplicateGroups": [
        {
            "hash": "d400842b76d06119ce603cb6aa63fd8f4d56eda773357186eab5ee46c43d20fb",
            "fileSize": 63,
            "wastedBytes": 126,
```

### `backup` — real output, two runs in a row

```
$ filewarden backup examples/sample_data /tmp/backup_demo --threads 4
Creating snapshot: "examples/sample_data" -> "/tmp/backup_demo"

Snapshot 20260802_230537 created.
  Files scanned:      9
  New files:          9
  Changed files:      0
  Unchanged files:    0 (0 skipped re-hashing via trusted mtime)
  Logical size:       431 B
  New bytes written:  195 B  (236 B saved by reuse/dedup)
```

431&nbsp;B of files, only 195&nbsp;B actually written — the sample data
contains the same duplicates `dedupe` found above, and the object store
deduplicates them automatically on the way in.

After editing one file and adding another:

```
$ filewarden backup examples/sample_data /tmp/backup_demo --threads 4
Creating snapshot: "examples/sample_data" -> "/tmp/backup_demo"

Snapshot 20260802_230538 created.
  Files scanned:      10
  New files:          1
  Changed files:      1
  Unchanged files:    8 (8 skipped re-hashing via trusted mtime)
  Logical size:       481 B
  New bytes written:  113 B  (368 B saved by reuse/dedup)
```

8 of 10 files were never re-read from disk at all — their size and
modification time matched the previous snapshot, so their previously
computed hash was trusted directly.

### `backup --compress` — real numbers on a realistic file

The sample data above is mostly small text files, too small for Huffman
coding's fixed header to pay off (see [design decision #4](#why-this-isnt-just-for-loop--delete)).
To show compression actually earning its keep, here's `--compress` against a
148&nbsp;KB synthetic application log (repetitive, skewed byte frequency —
exactly what Huffman coding is good at):

```
$ filewarden backup /tmp/app_logs /tmp/log_backup --compress
Creating snapshot: "/tmp/app_logs" -> "/tmp/log_backup"

Snapshot 20260803_205406 created.
  Files scanned:      1
  New files:          1
  Changed files:      0
  Unchanged files:    0 (0 skipped re-hashing via trusted mtime)
  Objects compressed: 1
  Logical size:       144.58 KB
  New bytes written:  91.88 KB  (52.70 KB saved by reuse/dedup/compression)
```

Without `--compress`, the same file writes all 144.58&nbsp;KB. With it,
36% fewer bytes hit disk — and `restore --verify` on that snapshot reproduces
the 148,052-byte file exactly, confirmed with `diff` against the original.

### `restore` — bringing back a point in time

```
$ filewarden restore /tmp/backup_demo 20260802_230537 /tmp/restored --verify
Restoring snapshot 20260802_230537 -> "/tmp/restored" (with integrity verification)
Restore complete.
```

Restoring the *older* snapshot correctly excludes the file that was added
afterwards, and `--verify` re-hashes every restored file against the manifest
to catch silent corruption in the object store before you find out the hard
way.

## Testing

```bash
cmake --build build --target filewarden_tests
./build/filewarden_tests
```

44 unit tests cover the SHA-256 implementation against known test vectors,
the scanner, all three stages of the duplicate-finding pipeline, the Huffman
codec's round-trip correctness (including empty input, single-symbol input,
all 256 byte values, and pseudo-random incompressible data), and the backup
engine's incremental detection, cross-file deduplication, compression
fallback behavior, and point-in-time restore correctness. The suite also
passes cleanly under **AddressSanitizer**, **UndefinedBehaviorSanitizer**,
and **ThreadSanitizer** — the last of which matters specifically because of
the shared thread pool used to parallelize hashing.

```bash
# Optional: run with sanitizers enabled
g++ -std=c++17 -O1 -g -pthread -fsanitize=address,undefined -Iinclude -Itests \
  src/sha256.cpp src/file_scanner.cpp src/duplicate_finder.cpp src/manifest.cpp \
  src/backup_engine.cpp src/huffman.cpp tests/*.cpp -o filewarden_tests_asan
./filewarden_tests_asan
```

## Design notes & known limitations

- SHA-256 here is used purely as a **content identifier** (the same role it
  plays in Git's object model) — not for any security or authentication
  purpose. There's no secret material involved.
- Hard links require the object store and the snapshot to live on the same
  filesystem. When that's not the case (or on filesystems that don't support
  hard links at all), FileWarden automatically falls back to a real copy for
  that file rather than failing the whole snapshot.
- The mtime-trust optimization in `backup` is a deliberate speed/certainty
  tradeoff — see [design decision #3](#why-this-isnt-just-for-loop--delete)
  above. Use `--full-rehash` to opt out of it entirely.
- `--compress` operates on whole files in memory rather than streaming (see
  [Architecture](#architecture)), and only helps for skewed byte-frequency
  data — it has no notion of repeated sequences the way LZ77/DEFLATE does.
  FileWarden always measures and falls back to raw storage when compression
  wouldn't help, so it's safe to leave on, just don't expect gzip-level
  ratios on already-compressed formats.
- Not yet implemented, and deliberately left out of this version rather than
  bolted on: perceptual/near-duplicate detection for images (finding two
  photos that *look* the same but aren't byte-identical is a fundamentally
  different, fuzzier problem than everything else here, and deserves its own
  design rather than being squeezed into a byte-exact content-addressing
  system).

## License

MIT — see [LICENSE](LICENSE).
