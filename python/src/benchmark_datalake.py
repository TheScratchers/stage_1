"""
Stage 1 - Benchmark: comparing the 3 datalake storage structures
(time-based, book-based, batch-based) implemented in Python.

Metrics measured for each structure:
  1. Write throughput  -> time to ingest N books (seconds, books/sec)
  2. Lookup cost        -> time to locate + read a sample of book_ids
  3. Storage overhead   -> number of directories created, avg files/dir,
                           max directory depth

To avoid depending on network access (Project Gutenberg is unreachable
from this environment), we reuse the real header/body text of the 3
books already downloaded into datalake/ (5, 11, 1342) and replicate
that content across many synthetic book_ids. This keeps the content
"real" (actual book text, actual sizes) while letting us simulate
volumes far beyond what we've manually downloaded.

Usage:
    python benchmark_datalake.py [n_books]

Results are printed to stdout and also written as JSON to
datamarts/benchmark_datalake_results.json
"""

import json
import random
import shutil
import sys
import time
from pathlib import Path

from download_book import save_book
from datalake import time_based_path
from datalake_book import book_based_path
from datalake_batch import batch_based_path

REAL_BOOKS_DIR = Path("datalake/20260917/18")
REAL_BOOK_IDS = [5, 11, 1342]
BENCH_ROOT = Path("bench_data")
N_LOOKUPS = 200


def load_real_content():
    """Loads header/body text for the 3 real books already in the datalake."""
    content = {}
    for book_id in REAL_BOOK_IDS:
        header = (REAL_BOOKS_DIR / f"{book_id}.header.txt").read_text(encoding="utf-8")
        body = (REAL_BOOKS_DIR / f"{book_id}.body.txt").read_text(encoding="utf-8")
        content[book_id] = (header, body)
    return content


def make_synthetic_ids(n_books: int, start_id: int = 100000):
    """Synthetic book_ids that don't collide with real Gutenberg ids."""
    return list(range(start_id, start_id + n_books))


def count_dirs_and_depth(root: Path):
    """Returns (num_dirs, max_depth, files_per_dir) for a directory tree."""
    if not root.exists():
        return 0, 0, []
    dirs = [p for p in root.rglob("*") if p.is_dir()]
    files_per_dir = {}
    for f in root.rglob("*.body.txt"):
        files_per_dir.setdefault(f.parent, 0)
        files_per_dir[f.parent] += 1
    max_depth = 0
    for p in root.rglob("*"):
        depth = len(p.relative_to(root).parts)
        max_depth = max(max_depth, depth)
    return len(dirs), max_depth, list(files_per_dir.values())


def benchmark_structure(name: str, path_fn, book_ids, content_by_real_id):
    """
    Ingests `book_ids` using `path_fn(base_dir, book_id)` to resolve the
    target directory, then measures write throughput, lookup cost and
    storage overhead for the resulting tree.
    """
    base_dir = BENCH_ROOT / name
    if base_dir.exists():
        shutil.rmtree(base_dir)
    base_dir.mkdir(parents=True)

    # 1. Write throughput
    t0 = time.perf_counter()
    for book_id in book_ids:
        # Cycle through the 3 real books so every synthetic id gets
        # genuine header/body content instead of empty/dummy text.
        real_id = REAL_BOOK_IDS[book_id % len(REAL_BOOK_IDS)]
        header, body = content_by_real_id[real_id]
        out_dir = path_fn(str(base_dir), book_id)
        save_book(book_id, header, body, str(out_dir))
    write_elapsed = time.perf_counter() - t0

    # 2. Lookup cost: pick N_LOOKUPS random ids, resolve path + read body
    sample_ids = random.sample(book_ids, min(N_LOOKUPS, len(book_ids)))
    t0 = time.perf_counter()
    bytes_read = 0
    for book_id in sample_ids:
        out_dir = path_fn(str(base_dir), book_id)
        body_path = Path(out_dir) / f"{book_id}.body.txt"
        bytes_read += len(body_path.read_text(encoding="utf-8"))
    lookup_elapsed = time.perf_counter() - t0

    # 3. Storage overhead
    num_dirs, max_depth, files_per_dir = count_dirs_and_depth(base_dir)
    avg_files_per_dir = (sum(files_per_dir) / len(files_per_dir)) if files_per_dir else 0
    max_files_per_dir = max(files_per_dir) if files_per_dir else 0

    return {
        "structure": name,
        "n_books": len(book_ids),
        "write_seconds": round(write_elapsed, 4),
        "write_books_per_sec": round(len(book_ids) / write_elapsed, 1) if write_elapsed > 0 else None,
        "lookup_n": len(sample_ids),
        "lookup_seconds": round(lookup_elapsed, 4),
        "lookup_avg_ms": round((lookup_elapsed / len(sample_ids)) * 1000, 4) if sample_ids else None,
        "num_dirs_created": num_dirs,
        "max_depth": max_depth,
        "avg_files_per_dir": round(avg_files_per_dir, 1),
        "max_files_per_dir": max_files_per_dir,
    }


def main():
    n_books = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    random.seed(42)

    print(f"Loading real content from {REAL_BOOKS_DIR} (books {REAL_BOOK_IDS})...")
    content_by_real_id = load_real_content()

    book_ids = make_synthetic_ids(n_books)
    print(f"Benchmarking {n_books} synthetic books across 3 datalake structures...\n")

    results = []
    # time_based_path() ignores book_id (every book ingested in the
    # same hour lands in the same directory) - the lambda just adapts
    # it to the same path_fn(base_dir, book_id) signature as the other
    # two structures below.
    results.append(benchmark_structure("time_based", lambda b, i: time_based_path(b), book_ids, content_by_real_id))
    results.append(benchmark_structure("book_based", book_based_path, book_ids, content_by_real_id))
    results.append(benchmark_structure("batch_based", batch_based_path, book_ids, content_by_real_id))

    header = f"{'Structure':<14}{'Write (s)':<12}{'Books/s':<10}{'Lookup avg (ms)':<18}{'#Dirs':<8}{'MaxDepth':<10}{'AvgFiles/Dir':<14}{'MaxFiles/Dir':<12}"
    print(header)
    print("-" * len(header))
    for r in results:
        print(
            f"{r['structure']:<14}{r['write_seconds']:<12}{r['write_books_per_sec']:<10}"
            f"{r['lookup_avg_ms']:<18}{r['num_dirs_created']:<8}{r['max_depth']:<10}"
            f"{r['avg_files_per_dir']:<14}{r['max_files_per_dir']:<12}"
        )

    out_path = Path("datamarts/benchmark_datalake_results.json")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"n_books": n_books, "results": results}, f, indent=2)
    print(f"\nResults written to {out_path}")

    # Cleanup the benchmark data (it's synthetic, no need to keep it around)
    shutil.rmtree(BENCH_ROOT, ignore_errors=True)


if __name__ == "__main__":
    main()
