"""
Stage 1 - Organizing the Datalake (time-based hierarchy)

Recommended layout (Section 3 of the guide):

    datalake/YYYYMMDD/HH/<BOOK_ID>.body.txt
    datalake/YYYYMMDD/HH/<BOOK_ID>.header.txt

where YYYYMMDD is the date of download and HH is the hour (24h format)
at the moment of ingestion.

Trade-off: cheap to write (all books ingested in the same hour share
one directory, so there's little directory-creation overhead), but
that same directory can accumulate a lot of files if many books are
ingested in a short time window - see benchmark_datalake.py for a
concrete comparison against the book-based and batch-based layouts.
"""

from datetime import datetime
from pathlib import Path

from download_book import download_book


def time_based_path(base_dir: str, when: datetime = None) -> Path:
    """
    Returns the datalake directory that corresponds to a given moment,
    following the time-based hierarchy: <base_dir>/YYYYMMDD/HH
    """
    # If no specific moment was given, use right now - this is the
    # normal case when ingesting a book as it's downloaded.
    when = when or datetime.now()
    date_str = when.strftime("%Y%m%d")
    hour_str = when.strftime("%H")
    return Path(base_dir) / date_str / hour_str


def ingest_book(book_id: int, base_dir: str = "datalake") -> bool:
    """
    Downloads a single book and stores it in the datalake using the
    time-based hierarchy (organized by the date/hour of ingestion).
    """
    # Resolve today's/this-hour's directory, then let download_book do
    # the actual fetching and file writing into it.
    output_dir = time_based_path(base_dir)
    return download_book(book_id, str(output_dir))


def ingest_books(book_ids, base_dir: str = "datalake") -> dict:
    """
    Downloads and stores multiple books. Returns a summary dict with
    the ids that succeeded and the ids that failed.
    """
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
    # to ingest, e.g. `python datalake.py 1342 5 11`; with no
    # arguments, falls back to ingesting book 1342 as a quick test.
    ids = [int(x) for x in sys.argv[1:]] or [1342]
    summary = ingest_books(ids)
    print(f"Ingested successfully: {summary['success']}")
    if summary["failed"]:
        print(f"Failed: {summary['failed']}")
