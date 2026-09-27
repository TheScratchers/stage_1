"""
Stage 1 - Control Layer

Minimal orchestrator that coordinates downloading and indexing books,
tracking progress with two control files:

    control/downloaded_books.txt  - ids of books already downloaded
    control/indexed_books.txt     - ids of books already indexed

Each call to control_pipeline_step() either indexes one pending book
or downloads a new one (never both), avoiding duplicate work, and
updates the control files to reflect what happened.
"""

import random
import sys
from pathlib import Path

from datalake import ingest_book
from metadata import init_db, upsert_book, parse_header
from inverted_index import load_index, save_index, update_index as update_json_index
from inverted_index_hierarchical import update_index as update_hier_index

CONTROL_PATH = Path("control")
DOWNLOADED = CONTROL_PATH / "downloaded_books.txt"
INDEXED = CONTROL_PATH / "indexed_books.txt"

# Upper bound used when picking a random book_id to download next -
# not every id in this range exists on Project Gutenberg (some are
# unassigned or use a different format), which is why the download
# step below tolerates failures and just tries the next candidate.
TOTAL_BOOKS = 70000
DB_PATH = "datamarts/books.db"
JSON_INDEX_PATH = "datamarts/inverted_index.json"
HIER_INDEX_ROOT = "datamarts/inverted_index"
DATALAKE_DIR = "datalake"


def _read_ids(path: Path) -> set:
    if not path.exists():
        return set()
    return {int(x) for x in path.read_text(encoding="utf-8").splitlines() if x.strip()}


def _append_id(path: Path, book_id: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a", encoding="utf-8") as f:
        f.write(f"{book_id}\n")


def _find_book_files(book_id: int, datalake_dir: str = DATALAKE_DIR):
    """
    Locates a book's body/header files anywhere inside the datalake.

    Uses rglob (recursive) instead of a direct path because the control
    layer doesn't know - and doesn't need to know - which datalake
    structure (time-based, book-based, batch-based, ...) was used to
    store this particular book.
    """
    body_matches = list(Path(datalake_dir).rglob(f"{book_id}.body.txt"))
    header_matches = list(Path(datalake_dir).rglob(f"{book_id}.header.txt"))
    if not body_matches or not header_matches:
        return None, None
    return body_matches[0], header_matches[0]


def index_book(book_id: int) -> bool:
    """
    Indexes a single already-downloaded book: parses its metadata into
    SQLite and incrementally updates the JSON and hierarchical
    inverted indexes (no full rebuild).
    """
    body_file, header_file = _find_book_files(book_id)
    if body_file is None:
        print(f"[CONTROL] Could not locate files for book {book_id} in the datalake.")
        return False

    init_db(DB_PATH)
    meta = parse_header(header_file.read_text(encoding="utf-8"))
    body_text = body_file.read_text(encoding="utf-8")

    upsert_book(
        DB_PATH, book_id, meta["title"], meta["author"], meta["language"],
        str(body_file), str(header_file),
    )

    # JSON index: load the current file (or start empty), apply the
    # incremental update in memory, then write the whole file back -
    # cheaper than a full rebuild, but still one write of the entire
    # index (see benchmark_inverted_index.py for why this differs from
    # the hierarchical structure below).
    json_index = load_index(JSON_INDEX_PATH) if Path(JSON_INDEX_PATH).exists() else {}
    json_index = update_json_index(json_index, book_id, body_text)
    save_index(json_index, JSON_INDEX_PATH)

    # Hierarchical index: only the files for this book's terms are
    # touched, everything else in the index is left alone.
    update_hier_index(book_id, body_text, HIER_INDEX_ROOT)

    return True


def control_pipeline_step() -> None:
    """
    One coordination step:
      1. If there are downloaded-but-not-indexed books, index the oldest one.
      2. Otherwise, download a new book, skipping ids already downloaded.
      3. Update the control files to reflect what happened.
    """
    CONTROL_PATH.mkdir(parents=True, exist_ok=True)

    downloaded = _read_ids(DOWNLOADED)
    indexed = _read_ids(INDEXED)
    ready_to_index = downloaded - indexed

    if ready_to_index:
        # Index before downloading: keeps the datamarts (metadata +
        # inverted indexes) caught up with whatever is already in the
        # datalake, instead of piling up an ever-growing backlog of
        # downloaded-but-unindexed books.
        book_id = sorted(ready_to_index)[0]
        print(f"[CONTROL] Indexing book {book_id}...")
        if index_book(book_id):
            _append_id(INDEXED, book_id)
            print(f"[CONTROL] Book {book_id} successfully indexed.")
        else:
            print(f"[CONTROL] Failed to index book {book_id}.")
    else:
        # Only try a handful of random candidates per step rather than
        # looping until one works: Gutenberg ids can be sparse/missing,
        # and this keeps a single step bounded instead of risking many
        # network calls in a row.
        for _ in range(10):
            candidate_id = random.randint(1, TOTAL_BOOKS)
            if candidate_id not in downloaded:
                print(f"[CONTROL] Downloading new book with ID {candidate_id}...")
                if ingest_book(candidate_id, DATALAKE_DIR):
                    _append_id(DOWNLOADED, candidate_id)
                    print(f"[CONTROL] Book {candidate_id} successfully downloaded.")
                else:
                    print(f"[CONTROL] Failed to download book {candidate_id}.")
                break


if __name__ == "__main__":
    steps = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    for _ in range(steps):
        control_pipeline_step()
