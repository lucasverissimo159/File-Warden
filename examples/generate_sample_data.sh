#!/usr/bin/env bash
# Generates examples/sample_data/, a small directory tree with a handful of
# duplicate files scattered across subfolders -- the kind of mess a real
# Downloads or Photos folder accumulates over time -- so you can try
# `filewarden dedupe` and `filewarden backup` immediately after building.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sample_data"

rm -rf "$ROOT"
mkdir -p "$ROOT/Photos/2024/Vacation"
mkdir -p "$ROOT/Photos/2024/Vacation/edited_copy"
mkdir -p "$ROOT/Documents/Reports"
mkdir -p "$ROOT/Documents/Reports/backup_someone_made_by_hand"
mkdir -p "$ROOT/Downloads"

# A "photo" (simulated as text) that got copied twice while organizing.
echo "JPEGDATA:: a beach sunset, 4032x3024, taken 2024-07-14" > "$ROOT/Photos/2024/Vacation/sunset.jpg"
cp "$ROOT/Photos/2024/Vacation/sunset.jpg" "$ROOT/Photos/2024/Vacation/edited_copy/sunset.jpg"
cp "$ROOT/Photos/2024/Vacation/sunset.jpg" "$ROOT/Downloads/sunset (1).jpg"

# A unique photo, no duplicates.
echo "JPEGDATA:: a mountain trail, 4032x3024, taken 2024-07-15" > "$ROOT/Photos/2024/Vacation/trail.jpg"

# A report that someone manually "backed up" into a sibling folder -- the
# classic reason Downloads/Documents folders end up full of duplicates.
echo "Q3 FINANCIAL REPORT - quarterly figures and analysis follow..." > "$ROOT/Documents/Reports/q3_report.docx"
cp "$ROOT/Documents/Reports/q3_report.docx" "$ROOT/Documents/Reports/backup_someone_made_by_hand/q3_report.docx"
cp "$ROOT/Documents/Reports/q3_report.docx" "$ROOT/Downloads/q3_report_FINAL_v2.docx"

# Two small files that happen to share a size but not content, to show the
# duplicate finder doesn't false-positive on that alone.
printf 'AAAAAAAAAA' > "$ROOT/Downloads/token_a.txt"
printf 'BBBBBBBBBB' > "$ROOT/Downloads/token_b.txt"

echo "Sample data created at: $ROOT"
find "$ROOT" -type f | sort
