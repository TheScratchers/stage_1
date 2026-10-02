"""
Stage 1 - Downloads the shared cross-language benchmark dataset (see
../shared/CONTRACT.md and ../shared/books.txt) into a dedicated,
stable folder - separate from the normal time-based datalake/ - so
every benchmark that needs "the 20 contract books" always finds them
in the exact same place, regardless of when they were downloaded.

Why a separate folder instead of reusing datalake/: the normal
ingestion pipeline organizes books by download date/hour
(datalake/YYYYMMDD/HH/), so books downloaded on different days end up
in different folders. The shared contract needs one fixed, predictable
location that only ever contains exactly the 20 agreed-upon books -
nothing else, regardless of what else has been downloaded before or
since for ad-hoc testing (e.g. earlier benchmark runs used book id 5,
which is NOT part of the contract - this script never touches it,
and it will never appear in datalake_shared/ either).

Idempotent: a book_id already present (both .header.txt and .body.txt
exist) is skipped, not re-downloaded. Real Gutenberg content never
changes for a given id, so re-downloading an id you already have
would just waste time and bandwidth for no benefit - this also makes
it safe to re-run this script as many times as you want, e.g. after
a partial failure, or after shared/books.txt gets updated with a
swapped-out book.

Usage (run from the python/ directory, same as the other scripts):
    cd python
    python src/download_shared_dataset.py
"""

import sys
from pathlib import Path

from download_book import download_book

BOOKS_LIST = Path("../shared/books.txt")
OUTPUT_DIR = Path("datalake_shared")


def read_book_ids(path: Path) -> list:
    """Parses the shared book id list, ignoring comments/blank lines."""
    ids = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        # Blank lines and "#" comments (the title/author reference
        # table in books.txt) are not book ids - skip them.
        if not line or line.startswith("#"):
            continue
        ids.append(int(line))
    return ids


def already_downloaded(book_id: int, output_dir: Path) -> bool:
    """
    A book counts as already downloaded only if BOTH its header and
    body files are present - a partial/failed previous attempt (e.g.
    network dropped mid-write) shouldn't be mistaken for a complete
    one and silently skipped.
    """
    header = output_dir / f"{book_id}.header.txt"
    body = output_dir / f"{book_id}.body.txt"
    return header.exists() and body.exists()


def main():
    if not BOOKS_LIST.exists():
        print(f"Could not find {BOOKS_LIST} - make sure you're running this from the python/ directory.")
        sys.exit(1)

    book_ids = read_book_ids(BOOKS_LIST)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"Shared dataset: {len(book_ids)} books listed in {BOOKS_LIST}")
    print(f"Target folder: {OUTPUT_DIR}/\n")

    skipped, downloaded, failed = [], [], []
    for book_id in book_ids:
        if already_downloaded(book_id, OUTPUT_DIR):
            skipped.append(book_id)
            print(f"[{book_id}] already downloaded - skipping")
            continue

        print(f"[{book_id}] downloading...")
        ok = download_book(book_id, str(OUTPUT_DIR))
        if ok:
            downloaded.append(book_id)
        else:
            failed.append(book_id)

    print("\n--- Summary ---")
    print(f"Already had:  {len(skipped)}  {skipped}")
    print(f"Downloaded:   {len(downloaded)}  {downloaded}")
    print(f"Failed:       {len(failed)}  {failed}")

    if failed:
        print(
            "\nSome books failed (network issue, or Gutenberg's "
            "header/footer markers didn't match for that id). Re-run "
            "this script again later - it will only retry the ones "
            "still missing, not the ones that already succeeded."
        )


if __name__ == "__main__":
    main()
