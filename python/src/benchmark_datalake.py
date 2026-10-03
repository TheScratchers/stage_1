"""
Stage 1 - Benchmark: comparing the 3 datalake storage structures
(time-based, book-based, batch-based) implemented in Python.

Uses the shared cross-language contract (../shared/CONTRACT.md,
books.txt): the same 20 real books and the same 3 synthetic scales
that Java and C++ must also use, so results are comparable across
languages (see shared_contract.py for how these are loaded).

Metrics measured for each structure, at each scale:
  1. Write throughput        -> time to ingest N books (seconds, books/sec)
  2. Lookup cost              -> time to locate + read a sample of book_ids
  3. Storage overhead         -> number of directories created, avg files/dir,
                                 max directory depth
  4. Incremental-processing  -> cost of detecting which of a small new
     cost                       batch of ids are already present vs. new,
                                 then ingesting just the new ones, on top
                                 of a structure that's already populated
  5. Recovery behavior        -> simulates a crash after half of n_books
                                 is written, then "resumes" and confirms
                                 the result is complete with no duplicated
                                 or lost documents
  6. Scalability               -> the trend across the 3 contract scales

To avoid depending on network access at benchmark time, we reuse the
real header/body text of the 20 contract books already downloaded
into datalake_shared/ (see download_shared_dataset.py) and replicate
that content across many synthetic book_ids. This keeps the content
"real" (actual book text, actual sizes) while letting us simulate
volumes far beyond the 20 books we actually have.

Usage:
    python benchmark_datalake.py [scales_csv]
        Default scales_csv = "100,1000,10000" (the contract's 3
        scales). Example: python benchmark_datalake.py 500,5000

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

from shared_contract import load_book_ids

# Where the 20 contract books already live (see download_shared_dataset.py).
REAL_BOOKS_DIR = Path("datalake_shared")
REAL_BOOK_IDS = load_book_ids()
# Scratch folder for everything this benchmark creates - kept separate
# from the real datalake/, datalake_book/ and datalake_batch/ folders
# so we never mix synthetic and real data.
BENCH_ROOT = Path("bench_data")
N_LOOKUPS = 200


def load_real_content():
    """Loads header/body text for the 20 contract books."""
    content = {}
    # Read each real book's header and body once up front, so the
    # write-throughput loop below only has to do in-memory work.
    for book_id in REAL_BOOK_IDS:
        header = (REAL_BOOKS_DIR / f"{book_id}.header.txt").read_text(encoding="utf-8")
        body = (REAL_BOOKS_DIR / f"{book_id}.body.txt").read_text(encoding="utf-8")
        content[book_id] = (header, body)
    return content


def make_synthetic_ids(n_books: int, start_id: int = 100000):
    """Synthetic book_ids that don't collide with real Gutenberg ids."""
    # start_id=100000 is well above any real Gutenberg id we use, so
    # these synthetic ids can never be confused with real ones.
    return list(range(start_id, start_id + n_books))


def count_dirs_and_depth(root: Path):
    """Returns (num_dirs, max_depth, files_per_dir) for a directory tree."""
    # Nothing was written (e.g. this structure wasn't benchmarked) -
    # return all-zero/empty results instead of erroring on a missing path.
    if not root.exists():
        return 0, 0, []
    # rglob("*") walks every file and folder recursively; is_dir()
    # filters that down to just the directories, for the total count.
    dirs = [p for p in root.rglob("*") if p.is_dir()]
    # Count how many *.body.txt files ended up in each parent
    # directory, to later compute avg/max files per directory.
    files_per_dir = {}
    for f in root.rglob("*.body.txt"):
        files_per_dir.setdefault(f.parent, 0)
        files_per_dir[f.parent] += 1
    # The deepest path (in path segments relative to root) tells us
    # how many nested folder levels this structure created.
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
    # Start from a clean, empty directory each time this benchmark
    # runs, so results aren't skewed by files left over from a
    # previous run.
    if base_dir.exists():
        shutil.rmtree(base_dir)
    base_dir.mkdir(parents=True)

    # 1. Write throughput
    t0 = time.perf_counter()
    for i, book_id in enumerate(book_ids):
        # Cycle through the 20 contract books so every synthetic id
        # gets genuine header/body content instead of empty/dummy text.
        real_id = REAL_BOOK_IDS[book_id % len(REAL_BOOK_IDS)]
        header, body = content_by_real_id[real_id]
        out_dir = path_fn(str(base_dir), book_id)
        save_book(book_id, header, body, str(out_dir))
        # Heartbeat for the larger scales - this structure is written
        # 9 times per full run (3 structures x 3 scales), and without
        # any output in between it's not obvious which one is running
        # or whether it's still making progress. 2000 matches this
        # loop's own pace (this benchmark writes roughly 200 books/sec,
        # much faster per book than the inverted-index benchmarks, so
        # a smaller interval like 200 would print almost every second -
        # too chatty for how fast this particular loop runs).
        if (i + 1) % 2000 == 0:
            print(f"    [{name}] ...wrote {i + 1}/{len(book_ids)} books")
    write_elapsed = time.perf_counter() - t0

    # 2. Lookup cost: pick N_LOOKUPS random ids, resolve path + read body
    sample_ids = random.sample(book_ids, min(N_LOOKUPS, len(book_ids)))
    t0 = time.perf_counter()
    bytes_read = 0
    for book_id in sample_ids:
        # Recompute the path the same way a real "find this book"
        # lookup would - no caching, so this measures the true cost.
        out_dir = path_fn(str(base_dir), book_id)
        body_path = Path(out_dir) / f"{book_id}.body.txt"
        bytes_read += len(body_path.read_text(encoding="utf-8"))
    lookup_elapsed = time.perf_counter() - t0

    # 3. Storage overhead
    num_dirs, max_depth, files_per_dir = count_dirs_and_depth(base_dir)
    # Guard against an empty files_per_dir list (division by zero) if
    # somehow no files were written.
    avg_files_per_dir = (sum(files_per_dir) / len(files_per_dir)) if files_per_dir else 0
    max_files_per_dir = max(files_per_dir) if files_per_dir else 0

    # 4. Incremental-processing cost: build a candidate list that mixes
    # ids already in the structure (the second half of book_ids) with a
    # small batch of genuinely new ones, then time how long it takes to
    # DETECT which candidates are new (PDF: "cost of detecting which
    # books are new and ready to be indexed") before ingesting just
    # those - exactly what control.py has to do against a real datalake.
    incremental_n = max(1, len(book_ids) // 10)
    last_id = book_ids[-1] if book_ids else -1
    incremental_candidates = book_ids[len(book_ids) // 2:] + list(
        range(last_id + 1, last_id + 1 + incremental_n)
    )
    t0 = time.perf_counter()
    new_ids = []
    for book_id in incremental_candidates:
        out_dir = path_fn(str(base_dir), book_id)
        body_path = Path(out_dir) / f"{book_id}.body.txt"
        if not body_path.exists():
            new_ids.append(book_id)
    incremental_detect_elapsed = time.perf_counter() - t0

    t0 = time.perf_counter()
    for book_id in new_ids:
        real_id = REAL_BOOK_IDS[book_id % len(REAL_BOOK_IDS)]
        header, body = content_by_real_id[real_id]
        out_dir = path_fn(str(base_dir), book_id)
        save_book(book_id, header, body, str(out_dir))
    incremental_write_elapsed = time.perf_counter() - t0

    # 5. Recovery behavior: wipe the structure and rebuild only the
    # "pre-crash" first half of book_ids, simulating an interruption
    # partway through the original ingestion. Then "resume" by walking
    # the FULL book_ids list again, skipping any id whose file already
    # exists (idempotent recovery) and only writing what's missing.
    shutil.rmtree(base_dir)
    base_dir.mkdir(parents=True)
    half = len(book_ids) // 2
    for book_id in book_ids[:half]:
        real_id = REAL_BOOK_IDS[book_id % len(REAL_BOOK_IDS)]
        header, body = content_by_real_id[real_id]
        out_dir = path_fn(str(base_dir), book_id)
        save_book(book_id, header, body, str(out_dir))

    t0 = time.perf_counter()
    resumed_count = 0
    for book_id in book_ids:
        out_dir = path_fn(str(base_dir), book_id)
        body_path = Path(out_dir) / f"{book_id}.body.txt"
        if body_path.exists():
            continue  # already written before the simulated crash - skip
        real_id = REAL_BOOK_IDS[book_id % len(REAL_BOOK_IDS)]
        header, body = content_by_real_id[real_id]
        save_book(book_id, header, body, str(out_dir))
        resumed_count += 1
    recovery_elapsed = time.perf_counter() - t0

    # Correctness check: exactly n_books *.body.txt files afterwards -
    # no duplicates (each id maps to one deterministic path, so a
    # duplicate would mean a path collision) and nothing lost.
    _, _, recovery_files_per_dir = count_dirs_and_depth(base_dir)
    recovery_ok = sum(recovery_files_per_dir) == len(book_ids)

    # Package every measurement into one dict, ready to print and to
    # serialize as JSON.
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
        "incremental_candidates": len(incremental_candidates),
        "incremental_new_found": len(new_ids),
        "incremental_detect_seconds": round(incremental_detect_elapsed, 4),
        "incremental_write_seconds": round(incremental_write_elapsed, 4),
        "recovery_resumed_count": resumed_count,
        "recovery_seconds": round(recovery_elapsed, 4),
        "recovery_ok": recovery_ok,
    }


def print_table(scale: int, results: list):
    # Simple fixed-width table so the comparison is readable straight
    # from the terminal, printed once per scale.
    print(f"\n--- {scale} books ---")
    header = f"{'Structure':<14}{'Write (s)':<12}{'Books/s':<10}{'Lookup avg (ms)':<18}{'#Dirs':<8}{'MaxDepth':<10}{'AvgFiles/Dir':<14}{'MaxFiles/Dir':<12}"
    print(header)
    print("-" * len(header))
    for r in results:
        print(
            f"{r['structure']:<14}{r['write_seconds']:<12}{r['write_books_per_sec']:<10}"
            f"{r['lookup_avg_ms']:<18}{r['num_dirs_created']:<8}{r['max_depth']:<10}"
            f"{r['avg_files_per_dir']:<14}{r['max_files_per_dir']:<12}"
        )

    # Separate block for the two newer metrics - they don't fit the
    # fixed-width table above without making it unreadable.
    print(f"\n{'Structure':<14}{'Incr. detect (s)':<18}{'Incr. write (s)':<18}{'Recovery (s)':<14}{'Recovery OK':<12}")
    print("-" * 76)
    for r in results:
        print(
            f"{r['structure']:<14}{r['incremental_detect_seconds']:<18}"
            f"{r['incremental_write_seconds']:<18}{r['recovery_seconds']:<14}{str(r['recovery_ok']):<12}"
        )


def main():
    # scales can be overridden from the command line: `python
    # benchmark_datalake.py 500,5000`; a fixed random seed makes the
    # lookup sample (and therefore the results) reproducible.
    scales_csv = sys.argv[1] if len(sys.argv) > 1 else "100,1000,10000"
    scales = [int(x) for x in scales_csv.split(",") if x.strip()]
    random.seed(42)

    print(f"Loading real content from {REAL_BOOKS_DIR} ({len(REAL_BOOK_IDS)} contract books)...")
    content_by_real_id = load_real_content()

    # Run the full 3-structure comparison once per contract scale, so
    # a single invocation produces the whole scalability picture.
    results_by_scale = {}
    for n_books in scales:
        print(f"\n=== Benchmarking {n_books} synthetic books across 3 datalake structures ===")
        book_ids = make_synthetic_ids(n_books)

        results = []
        # time_based_path() ignores book_id (every book ingested in the
        # same hour lands in the same directory) - the lambda just adapts
        # it to the same path_fn(base_dir, book_id) signature as the other
        # two structures below.
        results.append(benchmark_structure("time_based", lambda b, i: time_based_path(b), book_ids, content_by_real_id))
        results.append(benchmark_structure("book_based", book_based_path, book_ids, content_by_real_id))
        results.append(benchmark_structure("batch_based", batch_based_path, book_ids, content_by_real_id))

        print_table(n_books, results)
        results_by_scale[str(n_books)] = results

    # Persist the raw numbers too, so they can be referenced later
    # (e.g. in the written report) without re-running the benchmark.
    out_path = Path("datamarts/benchmark_datalake_results.json")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"scales": scales, "results_by_scale": results_by_scale}, f, indent=2)
    print(f"\nResults written to {out_path}")

    # Cleanup the benchmark data (it's synthetic, no need to keep it around)
    shutil.rmtree(BENCH_ROOT, ignore_errors=True)


if __name__ == "__main__":
    main()
