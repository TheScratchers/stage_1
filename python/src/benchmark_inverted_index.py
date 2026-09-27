"""
Stage 1 - Benchmark: comparing the 3 inverted index structures
(monolithic JSON, hierarchical folder-per-letter/file-per-term, and
MongoDB) implemented in Python.

Like benchmark_datalake.py, this reuses the real header/body text of
the 3 books already downloaded (5, 11, 1342), replicated across many
synthetic book_ids, so we get a realistic vocabulary without depending
on network access.

Metrics measured for each structure (see the "why these metrics"
notes below):
  1. Build time        -> time to build the full index from scratch
  2. Lookup cost        -> average time to resolve a single term
  3. Storage overhead   -> number of files/dirs, total size on disk

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
    python benchmark_inverted_index.py [n_books] [book_ids_csv]
        Benchmarks the JSON and hierarchical structures (no network
        or database required). Default n_books = 200, book_ids_csv =
        "5,11,1342" (all 3 real books already in the datalake).
        book_ids_csv lets you restrict the real content used to build
        the vocabulary (e.g. "5" alone) - useful on slow/networked
        filesystems, since the hierarchical structure writes one file
        per unique term and vocabulary size (not n_books) is what
        drives that cost.

    python benchmark_inverted_index.py --mongo [n_books] [mongo_uri] [book_ids_csv]
        Benchmarks the MongoDB structure only. Requires a running
        MongoDB instance (see inverted_index_mongo.py for how to start
        one with Docker) and the pymongo package.

Both modes MERGE their results into
    datamarts/benchmark_inverted_index_results.json
so running the JSON/hierarchical part and the MongoDB part separately
(e.g. from different machines) still produces one combined report.
"""

import json
import random
import shutil
import sys
import time
from pathlib import Path

import inverted_index as idx_json
import inverted_index_hierarchical as idx_hier

REAL_BOOKS_DIR = Path("datalake/20260917/18")
ALL_REAL_BOOK_IDS = [5, 11, 1342]
BENCH_ROOT = Path("bench_data_idx")
SYNTHETIC_DATALAKE = BENCH_ROOT / "datalake"
JSON_OUT = BENCH_ROOT / "inverted_index.json"
HIER_ROOT = BENCH_ROOT / "inverted_index_hier"
RESULTS_PATH = Path("datamarts/benchmark_inverted_index_results.json")
N_LOOKUPS = 200


def load_real_bodies(real_book_ids):
    """Loads the body text for the given real books already in the datalake."""
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
    if SYNTHETIC_DATALAKE.exists():
        shutil.rmtree(SYNTHETIC_DATALAKE)
    SYNTHETIC_DATALAKE.mkdir(parents=True)

    bodies = load_real_bodies(real_book_ids)
    book_ids = list(range(start_id, start_id + n_books))
    for book_id in book_ids:
        real_id = real_book_ids[book_id % len(real_book_ids)]
        text = bodies[real_id]
        (SYNTHETIC_DATALAKE / f"{book_id}.body.txt").write_text(text, encoding="utf-8")
    return book_ids


def dir_size_bytes(root: Path) -> int:
    return sum(p.stat().st_size for p in root.rglob("*") if p.is_file())


def benchmark_json(book_ids):
    print("Building JSON (monolithic) index...")
    t0 = time.perf_counter()
    index = idx_json.build_index(str(SYNTHETIC_DATALAKE))
    idx_json.save_index(index, str(JSON_OUT))
    build_seconds = time.perf_counter() - t0

    # "Cold load" cost: parsing the whole file back from disk, as a
    # fresh process would have to before it can answer any query.
    t0 = time.perf_counter()
    loaded = idx_json.load_index(str(JSON_OUT))
    load_seconds = time.perf_counter() - t0

    # In-memory lookup cost, once the index is already loaded.
    sample_terms = random.sample(list(loaded.keys()), min(N_LOOKUPS, len(loaded)))
    t0 = time.perf_counter()
    for term in sample_terms:
        idx_json.search(loaded, term)
    lookup_seconds = time.perf_counter() - t0

    return {
        "structure": "json_monolithic",
        "n_terms": len(loaded),
        "build_seconds": round(build_seconds, 4),
        "cold_load_seconds": round(load_seconds, 4),
        "in_memory_lookup_avg_ms": round((lookup_seconds / len(sample_terms)) * 1000, 5),
        "num_files": 1,
        "total_size_bytes": JSON_OUT.stat().st_size,
    }


def benchmark_hierarchical(book_ids):
    print("Building hierarchical (folder-per-letter/file-per-term) index...")
    t0 = time.perf_counter()
    n_terms = idx_hier.build_index(str(SYNTHETIC_DATALAKE), str(HIER_ROOT))
    build_seconds = time.perf_counter() - t0

    # Every lookup here always means opening a small file from disk -
    # there's no equivalent "load once" step.
    all_terms = [p.stem for p in HIER_ROOT.rglob("*.txt")]
    sample_terms = random.sample(all_terms, min(N_LOOKUPS, len(all_terms)))
    t0 = time.perf_counter()
    for term in sample_terms:
        idx_hier.search(term, str(HIER_ROOT))
    lookup_seconds = time.perf_counter() - t0

    num_files = len(all_terms)
    num_dirs = len([p for p in HIER_ROOT.iterdir() if p.is_dir()])

    return {
        "structure": "hierarchical",
        "n_terms": n_terms,
        "build_seconds": round(build_seconds, 4),
        "avg_lookup_ms": round((lookup_seconds / len(sample_terms)) * 1000, 5),
        "num_files": num_files,
        "num_dirs": num_dirs,
        "total_size_bytes": dir_size_bytes(HIER_ROOT),
    }


def benchmark_mongo(book_ids, uri):
    import inverted_index_mongo as idx_mongo

    print(f"Building MongoDB index at {uri} ...")
    t0 = time.perf_counter()
    n_terms = idx_mongo.build_index(str(SYNTHETIC_DATALAKE), uri)
    build_seconds = time.perf_counter() - t0

    client, collection = idx_mongo.get_collection(uri)
    try:
        all_terms = [d["term"] for d in collection.find({}, {"term": 1})]
    finally:
        client.close()

    sample_terms = random.sample(all_terms, min(N_LOOKUPS, len(all_terms)))
    t0 = time.perf_counter()
    for term in sample_terms:
        idx_mongo.search(term, uri)
    lookup_seconds = time.perf_counter() - t0

    return {
        "structure": "mongodb",
        "n_terms": n_terms,
        "build_seconds": round(build_seconds, 4),
        "avg_lookup_ms": round((lookup_seconds / len(sample_terms)) * 1000, 5),
    }


def merge_results(new_results: dict):
    RESULTS_PATH.parent.mkdir(parents=True, exist_ok=True)
    existing = {}
    if RESULTS_PATH.exists():
        existing = json.loads(RESULTS_PATH.read_text(encoding="utf-8"))
    existing.update(new_results)
    RESULTS_PATH.write_text(json.dumps(existing, indent=2), encoding="utf-8")
    return existing


def print_table(results_by_structure: dict):
    print(f"\n{'Structure':<18}{'Build (s)':<12}{'Lookup':<28}{'Files':<10}{'Size (KB)':<12}")
    print("-" * 80)
    for r in results_by_structure.values():
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
            size_kb = "n/a (db)"
            files = "n/a (db)"
        print(f"{r['structure']:<18}{r['build_seconds']:<12}{lookup_desc:<28}{str(files):<10}{str(size_kb):<12}")


def parse_book_ids(csv_str):
    return [int(x) for x in csv_str.split(",") if x.strip()]


def main():
    args = sys.argv[1:]
    random.seed(42)

    if args and args[0] == "--mongo":
        n_books = int(args[1]) if len(args) > 1 else 200
        uri = args[2] if len(args) > 2 else "mongodb://localhost:27017/"
        real_book_ids = parse_book_ids(args[3]) if len(args) > 3 else ALL_REAL_BOOK_IDS
        book_ids = build_synthetic_datalake(n_books, real_book_ids)
        result = benchmark_mongo(book_ids, uri)
        combined = merge_results({"mongodb": result})
    else:
        n_books = int(args[0]) if len(args) > 0 else 200
        real_book_ids = parse_book_ids(args[1]) if len(args) > 1 else ALL_REAL_BOOK_IDS
        book_ids = build_synthetic_datalake(n_books, real_book_ids)
        json_result = benchmark_json(book_ids)
        hier_result = benchmark_hierarchical(book_ids)
        combined = merge_results({
            "json_monolithic": json_result,
            "hierarchical": hier_result,
        })

    print_table(combined)
    print(f"\nResults written to {RESULTS_PATH}")

    shutil.rmtree(BENCH_ROOT, ignore_errors=True)


if __name__ == "__main__":
    main()
