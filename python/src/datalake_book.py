"""
Stage 1 - Datalake Structure Benchmark: Book-based hierarchy

Each book gets its own dedicated directory:

    <base_dir>/<BOOK_ID>/<BOOK_ID>.body.txt
    <base_dir>/<BOOK_ID>/<BOOK_ID>.header.txt

Trade-off: the opposite of the time-based layout - every book is
fully isolated (never sharing a directory with unrelated books), at
the cost of creating one directory per book, which adds overhead when
ingesting many books (see benchmark_datalake.py).
"""

from pathlib import Path

from download_book import download_book


def book_based_path(base_dir: str, book_id: int) -> Path:
    """Returns the directory dedicated to a single book: <base_dir>/<book_id>"""
    # str(book_id) turns the integer id into the matching folder name.
    return Path(base_dir) / str(book_id)


def ingest_book(book_id: int, base_dir: str = "datalake_book") -> bool:
    """Downloads a single book and stores it under its own directory."""
    # Resolve this book's dedicated directory, then let download_book
    # do the actual fetching and file writing into it.
    output_dir = book_based_path(base_dir, book_id)
    return download_book(book_id, str(output_dir))


def ingest_books(book_ids, base_dir: str = "datalake_book") -> dict:
    """Downloads and stores multiple books. Returns a success/failed summary."""
    results = {"success": [], "failed": []}
    # Ingest one book at a time, sorting each id into the matching
    # list depending on whether ingest_book() reported success.
    for book_id in book_ids:
        ok = ingest_book(book_id, base_dir)
        (results["success"] if ok else results["failed"]).append(book_id)
    return results


if __name__ == "__main__":
    import sys

    # Command-line entry point: every argument is treated as a book_id
    # to ingest, e.g. `python datalake_book.py 1342 5 11`; with no
    # arguments, falls back to ingesting book 1342 as a quick test.
    ids = [int(x) for x in sys.argv[1:]] or [1342]
    summary = ingest_books(ids)
    print(f"Ingested successfully: {summary['success']}")
    if summary["failed"]:
        print(f"Failed: {summary['failed']}")
