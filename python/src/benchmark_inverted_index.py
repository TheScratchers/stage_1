"""
Stage 1 - Benchmark: comparing the 3 inverted index structures
(monolithic JSON, hierarchical folder-per-letter/file-per-term, and
MongoDB) implemented in Python.

Uses the shared cross-language contract (../shared/CONTRACT.md,
books.txt, words.txt): the same 20 real books, the same 10 fixed
query words, and the same 3 synthetic scales that Java and C++ must
also use, so results are comparable across languages (see
shared_contract.py for how these are loaded).

Metrics measured for each structure (see the "why these metrics"
notes below):
  1. Build time        -> time to build the full index from scratch
  2. Query performance  -> average time to resolve each of the 10
                           contract words (../shared/words.txt) - a
                           fixed workload, not a random sample, so
                           every language measures the exact same
                           queries
  3. Storage overhead   -> number of files/dirs, total size on disk
  4. Update performance -> cost of adding ONE new book to an
                           already-built index via update_index(),
                           without rebuilding it from scratch (for
                           json_monolithic this also times the
                           full-file rewrite update_index() still
                           needs to persist, since that's the
                           structure's real update cost)
  5. Memory usage        -> peak Python memory (tracemalloc) used
                           while building each structure
  6. Scalability        -> the trend across the 3 contract scales
                           (100 / 1,000 / 10,000 books)

Why lookup cost is measured differently per structure:
  - Monolithic JSON: a lookup normally happens against an index
    already loaded in memory (an app loads the JSON once at startup).
    So we report BOTH the one-time "cold load" cost (parsing the full
    JSON file from disk) and the (near-zero) in-memory dict lookup
    cost once loaded. This is the honest way to represent it: cheap
    per-lookup, but with an upfront cost that grows with index size.
  - Hierarchical: there's no "load" step - a lookup always means
    opening one small file from disk. So we report only the average
    per-lookup disk-read cost, which stays roughly constant no matter
    how big the whole index gets.
  - MongoDB: same idea as hierarchical (no full in-memory load), each
    lookup is a query. Benchmarked separately by the user because this
    environment cannot reach a local MongoDB instance.

Usage:
    python benchmark_inverted_index.py [scales_csv] [book_ids_csv]
        Benchmarks the JSON and hierarchical structures (no network
        or database required) at every scale in scales_csv (default
        "100,1000,10000", the contract's 3 scales). book_ids_csv
        overrides the 20 contract books (e.g. "5" alone, for a quick
        local smoke test on slow/networked filesystems) - leave it
        out for an official, contract-compliant run.

    python benchmark_inverted_index.py --mongo [scales_csv] [mongo_uri] [book_ids_csv]
        Benchmarks the MongoDB structure only, at every scale.
        Requires a running MongoDB instance (see
        inverted_index_mongo.py for how to start one with Docker) and
        the pymongo package.

Both modes MERGE their results into
    datamarts/benchmark_inverted_index_results.json
so running the JSON/hierarchical part and the MongoDB part separately
(e.g. from different machines), or re-running just one scale, still
produces one combined report.
"""

import json
import shutil
import sys
import time
import tracemalloc
from pathlib import Path

import inverted_index as idx_json
import inverted_index_hierarchical as idx_hier

from shared_contract import load_book_ids, load_words

# Where the 20 contract books already live (see download_shared_dataset.py).
REAL_BOOKS_DIR = Path("datalake_shared")
ALL_REAL_BOOK_IDS = load_book_ids()
# The 10 fixed query words every language must use (../shared/words.txt).
QUERY_WORDS = load_words()
# Scratch folder for everything this benchmark creates - kept separate
# from the real datamarts/ output so we never mix synthetic and real
# data while a run is in progress (it's deleted again at the end).
BENCH_ROOT = Path("bench_data_idx")
SYNTHETIC_DATALAKE = BENCH_ROOT / "datalake"
JSON_OUT = BENCH_ROOT / "inverted_index.json"
HIER_ROOT = BENCH_ROOT / "inverted_index_hier"
# The one file that DOES survive after cleanup - the actual results,
# which is what gets committed to the repo as evidence.
RESULTS_PATH = Path("datamarts/benchmark_inverted_index_results.json")


def load_real_bodies(real_book_ids):
    """Loads the body text for the given real books already in datalake_shared/."""
    # Dict comprehension: reads each real book's body text once up
    # front, keyed by its book_id, so the synthetic-datalake loop
    # below never re-reads a real file twice.
    return {
        book_id: (REAL_BOOKS_DIR / f"{book_id}.body.txt").read_text(encoding="utf-8")
        for book_id in real_book_ids
    }


def build_synthetic_datalake(n_books: int, real_book_ids, start_id: int = 200000) -> list:
    """
    Writes n_books *.body.txt files (no headers - the indexers only
    read bodies), replicating the given real books' content across
    synthetic book_ids. Returns the list of synthetic ids created.
    """
    # Start from a clean, empty directory each time this benchmark
    # runs, so results aren't skewed by files left over from a
    # previous run.
    if SYNTHETIC_DATALAKE.exists():
        shutil.rmtree(SYNTHETIC_DATALAKE)
    SYNTHETIC_DATALAKE.mkdir(parents=True)

    bodies = load_real_bodies(real_book_ids)
    # start_id=200000 is well above any real Gutenberg id (and above
    # benchmark_datalake.py's own synthetic range), so these ids can
    # never be confused with real or other synthetic ones.
    book_ids = list(range(start_id, start_id + n_books))
    for book_id in book_ids:
        # Cycling through real_book_ids means the vocabulary is fixed
        # by CONTENT diversity (how many distinct real books we use),
        # not by n_books - adding more synthetic ids just adds more
        # book_ids to the postings lists of terms that already exist.
        real_id = real_book_ids[book_id % len(real_book_ids)]
        text = bodies[real_id]
        (SYNTHETIC_DATALAKE / f"{book_id}.body.txt").write_text(text, encoding="utf-8")
    return book_ids


def dir_size_bytes(root: Path) -> int:
    # Adds up the size of every file (not directories themselves)
    # anywhere under root, giving the total disk usage of that tree.
    return sum(p.stat().st_size for p in root.rglob("*") if p.is_file())


def benchmark_json(book_ids):
    print("  Building JSON (monolithic) index...")
    # Build time: scan every synthetic body file and write the whole
    # index out as a single JSON file. tracemalloc wraps this to
    # capture the peak Python memory this structure needs to build.
    tracemalloc.start()
    t0 = time.perf_counter()
    index = idx_json.build_index(str(SYNTHETIC_DATALAKE))
    idx_json.save_index(index, str(JSON_OUT))
    build_seconds = time.perf_counter() - t0
    _, peak_bytes = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    # "Cold load" cost: parsing the whole file back from disk, as a
    # fresh process would have to before it can answer any query.
    t0 = time.perf_counter()
    loaded = idx_json.load_index(str(JSON_OUT))
    load_seconds = time.perf_counter() - t0

    # In-memory lookup cost for the 10 fixed contract words. A word
    # absent from this run's vocabulary is still a valid (empty-
    # result) query to time, so we query all 10 regardless.
    t0 = time.perf_counter()
    for term in QUERY_WORDS:
        idx_json.search(loaded, term)
    lookup_seconds = time.perf_counter() - t0

    # Update performance: add ONE new book to the already-loaded index
    # via update_index() (cheap, in-memory), then persist it with
    # save_index() - which, for this structure, means rewriting the
    # WHOLE file, since there's no partial-write option. That full
    # rewrite is the structure's real update cost, and it's expected
    # to grow with index size (see Pablo's C++ report, which measures
    # the same trade-off: an 11s rewrite at 10,000 books).
    new_book_id = (book_ids[-1] + 1) if book_ids else 0
    sample_body = (SYNTHETIC_DATALAKE / f"{book_ids[0]}.body.txt").read_text(encoding="utf-8")
    t0 = time.perf_counter()
    idx_json.update_index(loaded, new_book_id, sample_body)
    update_memory_seconds = time.perf_counter() - t0
    t0 = time.perf_counter()
    idx_json.save_index(loaded, str(JSON_OUT))
    update_save_seconds = time.perf_counter() - t0

    # Package every measurement into one dict, ready to print and to
    # merge into the results file.
    return {
        "structure": "json_monolithic",
        "n_terms": len(loaded),
        "build_seconds": round(build_seconds, 4),
        "peak_memory_kb": round(peak_bytes / 1024, 1),
        "cold_load_seconds": round(load_seconds, 4),
        "in_memory_lookup_avg_ms": round((lookup_seconds / len(QUERY_WORDS)) * 1000, 5),
        "update_memory_seconds": round(update_memory_seconds, 5),
        "update_save_seconds": round(update_save_seconds, 4),
        "update_total_seconds": round(update_memory_seconds + update_save_seconds, 4),
        "num_files": 1,
        "total_size_bytes": JSON_OUT.stat().st_size,
    }


def benchmark_hierarchical(book_ids):
    print("  Building hierarchical (folder-per-letter/file-per-term) index...")
    # Build time: scan every synthetic body file and write one file
    # per unique term. tracemalloc wraps this to capture the peak
    # Python memory this structure needs to build.
    tracemalloc.start()
    t0 = time.perf_counter()
    n_terms = idx_hier.build_index(str(SYNTHETIC_DATALAKE), str(HIER_ROOT))
    build_seconds = time.perf_counter() - t0
    _, peak_bytes = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    # Every lookup here always means opening a small file from disk -
    # there's no equivalent "load once" step. Same 10 fixed words.
    t0 = time.perf_counter()
    for term in QUERY_WORDS:
        idx_hier.search(term, str(HIER_ROOT))
    lookup_seconds = time.perf_counter() - t0

    # Update performance: add ONE new book via update_index(), which
    # only touches the files of the terms that appear in it - no full
    # rebuild, unlike the monolithic JSON structure.
    new_book_id = (book_ids[-1] + 1) if book_ids else 0
    sample_body = (SYNTHETIC_DATALAKE / f"{book_ids[0]}.body.txt").read_text(encoding="utf-8")
    t0 = time.perf_counter()
    idx_hier.update_index(new_book_id, sample_body, str(HIER_ROOT))
    update_seconds = time.perf_counter() - t0

    # Storage overhead: total term-files written, and how many letter
    # subfolders (A/, B/, ...) they're spread across.
    all_terms = [p.stem for p in HIER_ROOT.rglob("*.txt")]
    num_files = len(all_terms)
    num_dirs = len([p for p in HIER_ROOT.iterdir() if p.is_dir()])

    # Package every measurement into one dict, ready to print and to
    # merge into the results file.
    return {
        "structure": "hierarchical",
        "n_terms": n_terms,
        "build_seconds": round(build_seconds, 4),
        "peak_memory_kb": round(peak_bytes / 1024, 1),
        "avg_lookup_ms": round((lookup_seconds / len(QUERY_WORDS)) * 1000, 5),
        "update_seconds": round(update_seconds, 5),
        "num_files": num_files,
        "num_dirs": num_dirs,
        "total_size_bytes": dir_size_bytes(HIER_ROOT),
    }


def benchmark_mongo(book_ids, uri):
    # Imported here (rather than at the top of the file) so that
    # running the JSON/hierarchical benchmarks never requires pymongo
    # or a live MongoDB instance to be available.
    import inverted_index_mongo as idx_mongo

    print(f"  Building MongoDB index at {uri} ...")
    # Build time: scan every synthetic body file and insert one
    # document per unique term into MongoDB. tracemalloc only sees
    # this process's own Python-side memory (building the postings
    # dict before sending it to MongoDB), not the separate MongoDB
    # server's memory - but that's still a fair, consistent "memory
    # this implementation needs" number across all 3 structures.
    tracemalloc.start()
    t0 = time.perf_counter()
    n_terms = idx_mongo.build_index(str(SYNTHETIC_DATALAKE), uri)
    build_seconds = time.perf_counter() - t0
    _, peak_bytes = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    # Same 10 fixed contract words.
    t0 = time.perf_counter()
    for term in QUERY_WORDS:
        idx_mongo.search(term, uri)
    lookup_seconds = time.perf_counter() - t0

    # Update performance: add ONE new book via update_index(), which
    # uses $addToSet per term instead of rebuilding the collection.
    new_book_id = (book_ids[-1] + 1) if book_ids else 0
    sample_body = (SYNTHETIC_DATALAKE / f"{book_ids[0]}.body.txt").read_text(encoding="utf-8")
    t0 = time.perf_counter()
    idx_mongo.update_index(new_book_id, sample_body, uri)
    update_seconds = time.perf_counter() - t0

    # Package every measurement into one dict, ready to print and to
    # merge into the results file. No file/dir/size metrics here,
    # since MongoDB manages its own on-disk storage.
    return {
        "structure": "mongodb",
        "n_terms": n_terms,
        "build_seconds": round(build_seconds, 4),
        "peak_memory_kb": round(peak_bytes / 1024, 1),
        "avg_lookup_ms": round((lookup_seconds / len(QUERY_WORDS)) * 1000, 5),
        "update_seconds": round(update_seconds, 5),
    }


def merge_results(scale: int, new_results: dict):
    """
    Loads whatever's already on disk and merges new_results into the
    entry for this one scale, leaving every other scale - and every
    other structure already recorded at this same scale - untouched.
    This is what lets the JSON/hierarchical run and the separate
    --mongo run (maybe on a different machine, maybe for a different
    scale) combine into one report instead of overwriting each other.
    """
    RESULTS_PATH.parent.mkdir(parents=True, exist_ok=True)
    existing = {"scales": [], "results_by_scale": {}}
    if RESULTS_PATH.exists():
        existing = json.loads(RESULTS_PATH.read_text(encoding="utf-8"))
        existing.setdefault("results_by_scale", {})

    scale_key = str(scale)
    existing["results_by_scale"].setdefault(scale_key, {})
    # dict.update() overwrites structures that already exist at this
    # scale and adds new ones, same merge behaviour as before, just
    # now scoped to one scale instead of the whole file.
    existing["results_by_scale"][scale_key].update(new_results)

    # Keep the top-level "scales" list in sync with whatever scales
    # actually have data, sorted for a stable/readable file.
    existing["scales"] = sorted(int(k) for k in existing["results_by_scale"].keys())

    RESULTS_PATH.write_text(json.dumps(existing, indent=2), encoding="utf-8")
    return existing["results_by_scale"][scale_key]


def print_table(scale: int, results_by_structure: dict):
    # Simple fixed-width table so the comparison is readable straight
    # from the terminal, printed once per scale.
    print(f"\n--- {scale} books ---")
    print(f"{'Structure':<18}{'Build (s)':<12}{'Lookup':<28}{'Files':<10}{'Size (KB)':<12}")
    print("-" * 80)
    for r in results_by_structure.values():
        # Each structure reports lookup cost differently (see the
        # module docstring), so build that column's text separately
        # per structure before printing the row.
        if r["structure"] == "json_monolithic":
            lookup_desc = f"load={r['cold_load_seconds']}s / mem={r['in_memory_lookup_avg_ms']}ms"
            size_kb = round(r["total_size_bytes"] / 1024, 1)
            files = r["num_files"]
        elif r["structure"] == "hierarchical":
            lookup_desc = f"{r['avg_lookup_ms']}ms/lookup"
            size_kb = round(r["total_size_bytes"] / 1024, 1)
            files = r["num_files"]
        else:  # mongodb
            lookup_desc = f"{r['avg_lookup_ms']}ms/lookup"
            # MongoDB doesn't expose per-file/on-disk size the way a
            # plain filesystem structure does, so these are marked
            # explicitly as not applicable instead of showing 0.
            size_kb = "n/a (db)"
            files = "n/a (db)"
        print(f"{r['structure']:<18}{r['build_seconds']:<12}{lookup_desc:<28}{str(files):<10}{str(size_kb):<12}")

    # Separate block for the two newer metrics (update performance,
    # memory usage) - they don't fit the table above without making it
    # unreadable, and json_monolithic's update cost has 2 parts
    # (in-memory + full-file rewrite) worth keeping distinguishable.
    print(f"\n{'Structure':<18}{'Peak mem (KB)':<16}{'Update (s)':<28}")
    print("-" * 62)
    for r in results_by_structure.values():
        if r["structure"] == "json_monolithic":
            update_desc = f"mem={r['update_memory_seconds']}s / save={r['update_save_seconds']}s"
        else:
            update_desc = f"{r['update_seconds']}s"
        print(f"{r['structure']:<18}{r['peak_memory_kb']:<16}{update_desc:<28}")


def parse_scales(csv_str):
    return [int(x) for x in csv_str.split(",") if x.strip()]


def parse_book_ids(csv_str):
    # Turns a comma-separated string like "11,84,1342" into the list
    # of ints; the "if x.strip()" guard skips empty entries (e.g. a
    # stray trailing comma).
    return [int(x) for x in csv_str.split(",") if x.strip()]


def main():
    args = sys.argv[1:]

    print(f"Query workload: {len(QUERY_WORDS)} fixed words from ../shared/words.txt {QUERY_WORDS}")

    if args and args[0] == "--mongo":
        # --mongo mode: `python benchmark_inverted_index.py --mongo
        # [scales_csv] [mongo_uri] [book_ids_csv]` - only builds and
        # benchmarks the MongoDB structure, at every scale.
        scales_csv = args[1] if len(args) > 1 else "100,1000,10000"
        uri = args[2] if len(args) > 2 else "mongodb://localhost:27017/"
        real_book_ids = parse_book_ids(args[3]) if len(args) > 3 else ALL_REAL_BOOK_IDS
        scales = parse_scales(scales_csv)

        for scale in scales:
            print(f"\n=== {scale} books (MongoDB) ===")
            book_ids = build_synthetic_datalake(scale, real_book_ids)
            result = benchmark_mongo(book_ids, uri)
            combined = merge_results(scale, {"mongodb": result})
            print_table(scale, combined)
    else:
        # Default mode: `python benchmark_inverted_index.py [scales_csv]
        # [book_ids_csv]` - builds and benchmarks both the JSON and
        # hierarchical structures together, at every scale.
        scales_csv = args[0] if len(args) > 0 else "100,1000,10000"
        real_book_ids = parse_book_ids(args[1]) if len(args) > 1 else ALL_REAL_BOOK_IDS
        scales = parse_scales(scales_csv)

        for scale in scales:
            print(f"\n=== {scale} books (JSON + hierarchical) ===")
            book_ids = build_synthetic_datalake(scale, real_book_ids)
            json_result = benchmark_json(book_ids)
            hier_result = benchmark_hierarchical(book_ids)
            combined = merge_results(scale, {
                "json_monolithic": json_result,
                "hierarchical": hier_result,
            })
            print_table(scale, combined)

    print(f"\nResults written to {RESULTS_PATH}")

    # Remove the synthetic datalake/index files this run created -
    # only the merged results file (outside BENCH_ROOT) is kept.
    shutil.rmtree(BENCH_ROOT, ignore_errors=True)


if __name__ == "__main__":
    main()
