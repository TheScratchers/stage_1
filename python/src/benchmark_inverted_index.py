"""
Stage 1 - Benchmark: comparing the 3 inverted index structures
(monolithic JSON, hierarchical folder-per-letter/file-per-term, and
SQLite) implemented in Python.

Uses the shared cross-language contract (../shared/CONTRACT.md,
books.txt, words.txt): the same 20 real books, the same 10 fixed
query words, and the same 3 synthetic scales that Java and C++ must
also use, so results are comparable across languages (see
shared_contract.py for how these are loaded).

The team agreed all three languages (Python, Java, C++) would use the
same 3rd inverted-index structure, so the comparison across languages
is meaningful: monolithic JSON, hierarchical folder/file, and SQLite
(mirroring Java's SqliteIndexStorage schema - see
inverted_index_sqlite.py). MongoDB was dropped entirely (it was heavy
and unreliable on one team member's machine, and needed a separate
run against a live database anyway).

Metrics measured for each structure (see the "why these metrics"
notes below):
  1. Build time        -> time to build the full index from scratch
  2. Query performance  -> average time to resolve each of the 10
                           contract words (../shared/words.txt) - a
                           fixed workload, not a random sample, so
                           every language measures the exact same
                           queries
  3. Storage overhead   -> number of files/dirs (or just total size
                           for SQLite's single .db file), total size
                           on disk
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
  - SQLite: same idea as hierarchical (no full in-memory load), each
    lookup is a query executed by SQLite's own query engine against
    the on-disk (term, book_id) table, using the index its composite
    primary key creates automatically.

Usage:
    python benchmark_inverted_index.py [scales_csv] [book_ids_csv]
        Benchmarks all 3 structures (JSON, hierarchical, SQLite - no
        network or external database required) at every scale in
        scales_csv (default "100,1000,10000", the contract's 3
        scales). book_ids_csv overrides the 20 contract books (e.g.
        "5" alone, for a quick local smoke test on slow/networked
        filesystems) - leave it out for an official, contract-
        compliant run.

Results are written/merged into
    datamarts/benchmark_inverted_index_results.json
so re-running just one scale still produces one combined report
without losing the other scales' results.
"""

import json
import shutil
import sys
import tempfile
import time
import tracemalloc
from pathlib import Path

import inverted_index as idx_json
import inverted_index_hierarchical as idx_hier
import inverted_index_sqlite as idx_sqlite

from shared_contract import load_book_ids, load_words

# Where the 20 contract books already live (see download_shared_dataset.py).
REAL_BOOKS_DIR = Path("datalake_shared")
ALL_REAL_BOOK_IDS = load_book_ids()
# The 10 fixed query words every language must use (../shared/words.txt).
QUERY_WORDS = load_words()
# Scratch folder for everything this benchmark creates - kept separate
# from the real datamarts/ output so we never mix synthetic and real
# data while a run is in progress (it's deleted again at the end).
# Deliberately OUTSIDE the project folder (which lives inside OneDrive
# on this machine) and not just gitignored: these are disposable
# synthetic files recreated and deleted every run, never read by
# anything else. Living inside a cloud-synced folder made cleanup
# between scales intermittently fail with a Windows PermissionError
# (the sync client can still hold a brief lock on a folder this script
# just finished emptying) - the OS temp dir isn't watched by any sync
# client, so that race can't happen here.
BENCH_ROOT = Path(tempfile.gettempdir()) / "big_data_bench_inverted_index"
SYNTHETIC_DATALAKE = BENCH_ROOT / "datalake"
JSON_OUT = BENCH_ROOT / "inverted_index.json"
HIER_ROOT = BENCH_ROOT / "inverted_index_hier"
SQLITE_OUT = BENCH_ROOT / "inverted_index.db"
# The one file that DOES survive after cleanup - the actual results,
# which is what gets committed to the repo as evidence.
RESULTS_PATH = Path("datamarts/benchmark_inverted_index_results.json")


def safe_rmtree(path: Path, retries: int = 10, delay: float = 0.5) -> None:
    """
    shutil.rmtree() that tolerates the transient "PermissionError:
    [WinError 5] Acceso denegado" Windows raises when OneDrive (or
    Search Indexer / antivirus) still has a handle open on a folder
    this script just finished writing thousands of small files into -
    it fails only on the final rmdir, after every file inside has
    already been removed, and normally clears within a second or two.
    Retries with a short, increasing wait instead of crashing the
    whole benchmark run over a lock that isn't ours.
    """
    for attempt in range(retries):
        try:
            shutil.rmtree(path)
            return
        except PermissionError:
            if attempt == retries - 1:
                raise
            time.sleep(delay * (attempt + 1))


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
        safe_rmtree(SYNTHETIC_DATALAKE)
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
    # Start from a clean, empty directory each time, same reasoning as
    # SYNTHETIC_DATALAKE above: build_index() only ever adds/overwrites
    # term files, it never starts from empty, so without this the
    # folder accumulates stale term files across scales/runs forever -
    # skewing storage-overhead numbers and piling up file churn for
    # OneDrive to sync.
    if HIER_ROOT.exists():
        safe_rmtree(HIER_ROOT)
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


def benchmark_sqlite(book_ids):
    print("  Building SQLite index...")
    # Start from a clean .db file each time, same reasoning as JSON_OUT
    # and HIER_ROOT above: build_index() does its own "DELETE FROM
    # inverted_index" internally, but removing the file too keeps
    # storage-overhead numbers (total_size_bytes) from ever reflecting
    # SQLite's internal page reuse/fragmentation left over from a
    # previous run at a different scale.
    if SQLITE_OUT.exists():
        SQLITE_OUT.unlink()
    # Build time: scan every synthetic body file and insert one row
    # per (term, book_id) posting. tracemalloc wraps this to capture
    # the peak Python memory this structure needs to build (the
    # postings dict held in memory before being sent to SQLite) - the
    # same basis of comparison used for the other two structures.
    tracemalloc.start()
    t0 = time.perf_counter()
    n_terms = idx_sqlite.build_index(str(SYNTHETIC_DATALAKE), str(SQLITE_OUT))
    build_seconds = time.perf_counter() - t0
    _, peak_bytes = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    # Every lookup here is a SQL query against the on-disk (term,
    # book_id) table - no equivalent "load once" step, same as
    # hierarchical. Same 10 fixed words.
    t0 = time.perf_counter()
    for term in QUERY_WORDS:
        idx_sqlite.search(term, str(SQLITE_OUT))
    lookup_seconds = time.perf_counter() - t0

    # Update performance: add ONE new book via update_index(), which
    # batches one INSERT OR IGNORE per term into a single transaction -
    # no full rebuild, unlike the monolithic JSON structure.
    new_book_id = (book_ids[-1] + 1) if book_ids else 0
    sample_body = (SYNTHETIC_DATALAKE / f"{book_ids[0]}.body.txt").read_text(encoding="utf-8")
    t0 = time.perf_counter()
    idx_sqlite.update_index(new_book_id, sample_body, str(SQLITE_OUT))
    update_seconds = time.perf_counter() - t0

    # Package every measurement into one dict, ready to print and to
    # merge into the results file. Storage overhead is just the single
    # .db file's size - no per-term file count the way hierarchical
    # has, so num_files is reported as 1 for a consistent column.
    return {
        "structure": "sqlite",
        "n_terms": n_terms,
        "build_seconds": round(build_seconds, 4),
        "peak_memory_kb": round(peak_bytes / 1024, 1),
        "avg_lookup_ms": round((lookup_seconds / len(QUERY_WORDS)) * 1000, 5),
        "update_seconds": round(update_seconds, 5),
        "num_files": 1,
        "total_size_bytes": SQLITE_OUT.stat().st_size,
    }


def merge_results(scale: int, new_results: dict):
    """
    Loads whatever's already on disk and merges new_results into the
    entry for this one scale, leaving every other scale - and every
    other structure already recorded at this same scale - untouched.
    This is what lets re-running just one scale update that scale's
    numbers without losing the others already in the results file.
    """
    RESULTS_PATH.parent.mkdir(parents=True, exist_ok=True)
    existing = {"scales": [], "results_by_scale": {}}
    if RESULTS_PATH.exists():
        existing = json.loads(RESULTS_PATH.read_text(encoding="utf-8"))
        existing.setdefault("results_by_scale", {})

    scale_key = str(scale)
    existing["results_by_scale"].setdefault(scale_key, {})
    # Drop any structure this codebase no longer produces (e.g. a
    # "mongodb" entry written by a run from before the team dropped
    # MongoDB) - an old entry like that is missing keys the current
    # print_table() always expects (total_size_bytes, num_files),
    # since it belongs to a structure benchmark_*() no longer
    # populates. Carrying it forward silently would either crash
    # print_table() with a KeyError or display stale, no-longer-true
    # numbers next to the 3 structures actually being compared now.
    stale_structures = set(existing["results_by_scale"][scale_key]) - {"json_monolithic", "hierarchical", "sqlite"}
    for stale in stale_structures:
        del existing["results_by_scale"][scale_key][stale]
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
        else:  # hierarchical, sqlite
            lookup_desc = f"{r['avg_lookup_ms']}ms/lookup"
        size_kb = round(r["total_size_bytes"] / 1024, 1)
        files = r["num_files"]
        print(f"{r['structure']:<18}{r['build_seconds']:<12}{lookup_desc:<28}{str(files):<10}{str(size_kb):<12}")

    # Separate block for the two newer metrics (update performance,
    # memory usage) - they don't fit the table above without making it
    # unreadable, and json_monolithic's update cost has 2 parts
    # (in-memory + full-file rewrite) worth keeping distinguishable.
    print(f"\n{'Structure':<18}{'Peak mem (KB)':<16}{'Update (s)':<28}")
    print("-" * 62)
    for r in results_by_structure.values():
        # .get(..., "n/a") rather than r[...]: a scale's merged entry
        # can still hold an older result (e.g. from before these 2
        # metrics existed, or a stale "mongodb" entry from before the
        # team dropped it) that simply doesn't have these keys yet -
        # showing "n/a" for that one row is correct, a KeyError crash
        # is not. Delete/regenerate datamarts/
        # benchmark_inverted_index_results.json for a fully fresh file.
        if r["structure"] == "json_monolithic":
            if "update_memory_seconds" in r:
                update_desc = f"mem={r['update_memory_seconds']}s / save={r['update_save_seconds']}s"
            else:
                update_desc = "n/a (stale result)"
        else:
            update_desc = f"{r['update_seconds']}s" if "update_seconds" in r else "n/a (stale result)"
        print(f"{r['structure']:<18}{r.get('peak_memory_kb', 'n/a'):<16}{update_desc:<28}")


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

    # Single mode now: `python benchmark_inverted_index.py [scales_csv]
    # [book_ids_csv]` builds and benchmarks all 3 structures (JSON,
    # hierarchical, SQLite) together, at every scale, in one run - no
    # separate --mongo pass needed anymore, since SQLite needs no
    # external service to be running.
    scales_csv = args[0] if len(args) > 0 else "100,1000,10000"
    real_book_ids = parse_book_ids(args[1]) if len(args) > 1 else ALL_REAL_BOOK_IDS
    scales = parse_scales(scales_csv)

    for scale in scales:
        print(f"\n=== {scale} books (JSON + hierarchical + SQLite) ===")
        book_ids = build_synthetic_datalake(scale, real_book_ids)
        json_result = benchmark_json(book_ids)
        hier_result = benchmark_hierarchical(book_ids)
        sqlite_result = benchmark_sqlite(book_ids)
        combined = merge_results(scale, {
            "json_monolithic": json_result,
            "hierarchical": hier_result,
            "sqlite": sqlite_result,
        })
        print_table(scale, combined)

    print(f"\nResults written to {RESULTS_PATH}")

    # Remove the synthetic datalake/index files this run created -
    # only the merged results file (outside BENCH_ROOT) is kept.
    try:
        safe_rmtree(BENCH_ROOT)
    except Exception as e:
        print(f"Warning: couldn't fully clean up {BENCH_ROOT} ({e}); "
              f"delete it manually when convenient - it has no effect "
              f"on the results already written to {RESULTS_PATH}.")


if __name__ == "__main__":
    main()
