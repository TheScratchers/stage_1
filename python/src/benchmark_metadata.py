"""
Stage 1 - Benchmark: stress-testing the metadata datamart (SQLite)
at increasing scale.

This is a team extension beyond the project spec (see shared/CONTRACT.md
Section 5): the spec does NOT require comparing metadata storage
across languages, but since Python, Java and C++ are all benchmarking
their own metadata storage choice this way, this script follows the
same shared contract (20 real books, 3 synthetic scales, 2 fixed
author queries) as the required datalake and inverted-index
benchmarks, so the numbers stay comparable across languages.

Metrics measured at each scale:
  1. Insertion speed  -> time to upsert N books (seconds, books/sec)
  2. Query performance -> avg time for find_by_id (primary-key lookup)
                          and find_by_author (LIKE substring match),
                          using the 2 fixed authors from the contract:
                          Charles Dickens (repeated - 3 of the 20
                          books) and Lewis Carroll (unique - 1 of the
                          20 books), measured separately so the
                          "bigger result set costs more" effect is
                          visible instead of averaged away.
  3. Scalability       -> the trend across scales IS this metric: we
                          run the same experiments at 1,000, 10,000
                          and 100,000 books and compare how insertion
                          and query times change as the table grows.

Uses the 20 real contract books already downloaded into
datalake_shared/ (see download_shared_dataset.py), replicating their
title/author/language across many synthetic book_ids - exactly like
benchmark_datalake.py and benchmark_inverted_index.py do.

This calls metadata.upsert_book() / find_by_id() / find_by_author()
directly - the same functions index_metadata() and control.py use -
so the benchmark measures our actual implementation (including its
per-call sqlite3.connect()/commit()/close() overhead), not an
idealized bulk-insert number.

Usage:
    python benchmark_metadata.py [scales_csv]
        Default scales_csv = "1000,10000,100000" (the contract's 3
        scales). Example: python benchmark_metadata.py 500,5000

Results are printed to stdout and also written as JSON to
datamarts/benchmark_metadata_results.json
"""

import json
import random
import shutil
import sys
import time
from pathlib import Path

import metadata as md
from shared_contract import load_book_ids, METADATA_REPEATED_AUTHOR, METADATA_UNIQUE_AUTHOR

# Where the 20 contract books already live (see download_shared_dataset.py).
REAL_BOOKS_DIR = Path("datalake_shared")
REAL_BOOK_IDS = load_book_ids()
# Scratch folder for every per-scale SQLite file this benchmark
# creates - kept separate from the real datamarts/books.db so we
# never touch real data.
BENCH_ROOT = Path("bench_data_metadata")
N_ID_LOOKUPS = 200
# How many times to repeat each of the 2 fixed author queries, per scale.
AUTHOR_QUERY_REPS = 30


def load_real_metadata():
    """Parses title/author/language from the 20 contract books' headers."""
    # Read + parse each real book's header once up front, so the
    # insertion loop below only has to do in-memory work per book.
    content = {}
    for book_id in REAL_BOOK_IDS:
        header_text = (REAL_BOOKS_DIR / f"{book_id}.header.txt").read_text(encoding="utf-8")
        content[book_id] = md.parse_header(header_text)
    return content


def make_synthetic_ids(n_books: int, start_id: int = 500000):
    """Synthetic book_ids that don't collide with real Gutenberg ids
    or with the other benchmarks' synthetic ranges (100000, 200000)."""
    return list(range(start_id, start_id + n_books))


def benchmark_scale(n_books: int, meta_by_real_id: dict):
    """
    Upserts n_books synthetic rows (cycling through the 20 contract
    books' metadata) into a fresh SQLite file, then measures insertion
    speed and query performance (find_by_id, find_by_author) against it.
    """
    db_path = str(BENCH_ROOT / f"metadata_{n_books}.db")
    # Start from a clean database each time this scale runs, so
    # results aren't skewed by rows left over from a previous run.
    Path(db_path).unlink(missing_ok=True)
    md.init_db(db_path)

    book_ids = make_synthetic_ids(n_books)

    # 1. Insertion speed
    t0 = time.perf_counter()
    for i, book_id in enumerate(book_ids):
        real_id = REAL_BOOK_IDS[book_id % len(REAL_BOOK_IDS)]
        meta = meta_by_real_id[real_id]
        md.upsert_book(
            db_path,
            book_id,
            meta["title"],
            meta["author"],
            meta["language"],
            f"datalake/.../{book_id}.body.txt",
            f"datalake/.../{book_id}.header.txt",
        )
        # Progress heartbeat for the larger scales, so a 100,000-row
        # run doesn't look stuck for several minutes with no output.
        if (i + 1) % 20000 == 0:
            print(f"  ...inserted {i + 1}/{n_books}")
    insert_elapsed = time.perf_counter() - t0

    # 2. Query performance - find_by_id (primary-key lookup)
    sample_ids = random.sample(book_ids, min(N_ID_LOOKUPS, len(book_ids)))
    t0 = time.perf_counter()
    for book_id in sample_ids:
        md.find_by_id(db_path, book_id)
    id_lookup_elapsed = time.perf_counter() - t0

    # 2b. Query performance - find_by_author (LIKE substring scan),
    # using the contract's 2 fixed authors, timed SEPARATELY: Dickens
    # appears in 3 of the 20 books (bigger matching set as n_books
    # grows), Carroll in just 1 (minimal matching set) - averaging
    # them together would hide exactly the effect we want to see.
    t0 = time.perf_counter()
    for _ in range(AUTHOR_QUERY_REPS):
        md.find_by_author(db_path, METADATA_REPEATED_AUTHOR)
    repeated_author_elapsed = time.perf_counter() - t0

    t0 = time.perf_counter()
    for _ in range(AUTHOR_QUERY_REPS):
        md.find_by_author(db_path, METADATA_UNIQUE_AUTHOR)
    unique_author_elapsed = time.perf_counter() - t0

    db_size_bytes = Path(db_path).stat().st_size

    return {
        "n_books": n_books,
        "insert_seconds": round(insert_elapsed, 4),
        "insert_books_per_sec": round(n_books / insert_elapsed, 1) if insert_elapsed > 0 else None,
        "find_by_id_avg_ms": round((id_lookup_elapsed / len(sample_ids)) * 1000, 4) if sample_ids else None,
        "find_by_author_repeated_avg_ms": round((repeated_author_elapsed / AUTHOR_QUERY_REPS) * 1000, 4),
        "find_by_author_unique_avg_ms": round((unique_author_elapsed / AUTHOR_QUERY_REPS) * 1000, 4),
        "db_size_bytes": db_size_bytes,
    }


def parse_scales(csv_str: str):
    return [int(x) for x in csv_str.split(",") if x.strip()]


def main():
    scales_csv = sys.argv[1] if len(sys.argv) > 1 else "1000,10000,100000"
    scales = parse_scales(scales_csv)
    random.seed(42)

    BENCH_ROOT.mkdir(parents=True, exist_ok=True)

    print(f"Loading real metadata from {REAL_BOOKS_DIR} ({len(REAL_BOOK_IDS)} contract books)...")
    meta_by_real_id = load_real_metadata()
    print(f"find_by_author queries: repeated='{METADATA_REPEATED_AUTHOR}', unique='{METADATA_UNIQUE_AUTHOR}'")

    print(f"Benchmarking SQLite metadata datamart at scales: {scales}\n")

    results = []
    for n_books in scales:
        print(f"-- {n_books} books --")
        results.append(benchmark_scale(n_books, meta_by_real_id))

    # Print a simple fixed-width table so the scalability trend is
    # readable straight from the terminal, across all scales at once.
    header = (
        f"{'N books':<12}{'Insert (s)':<12}{'Books/s':<10}{'find_by_id (ms)':<18}"
        f"{'author:repeated (ms)':<22}{'author:unique (ms)':<20}{'DB size (KB)':<14}"
    )
    print("\n" + header)
    print("-" * len(header))
    for r in results:
        print(
            f"{r['n_books']:<12}{r['insert_seconds']:<12}{r['insert_books_per_sec']:<10}"
            f"{r['find_by_id_avg_ms']:<18}{r['find_by_author_repeated_avg_ms']:<22}"
            f"{r['find_by_author_unique_avg_ms']:<20}{round(r['db_size_bytes'] / 1024, 1):<14}"
        )

    # Persist the raw numbers too, so they can be referenced later
    # (e.g. in the written report) without re-running the benchmark.
    out_path = Path("datamarts/benchmark_metadata_results.json")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"scales": scales, "results": results}, f, indent=2)
    print(f"\nResults written to {out_path}")

    # Cleanup the benchmark databases (they're synthetic, no need to
    # keep them around) - only the results JSON above survives.
    shutil.rmtree(BENCH_ROOT, ignore_errors=True)


if __name__ == "__main__":
    main()
