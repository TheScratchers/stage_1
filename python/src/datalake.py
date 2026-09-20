"""
Stage 1 - Organizing the Datalake (time-based hierarchy)

Recommended layout (Section 3 of the guide):

    datalake/YYYYMMDD/HH/<BOOK_ID>.body.txt
    datalake/YYYYMMDD/HH/<BOOK_ID>.header.txt

where YYYYMMDD is the date of download and HH is the hour (24h format)
at the moment of ingestion.
"""

from datetime import datetime
from pathlib import Path

from download_book import download_book


def time_based_path(base_dir: str, when: datetime = None) -> Path:
    """
    Returns the datalake directory that corresponds to a given moment,
    following the time-based hierarchy: <base_dir>/YYYYMMDD/HH
    """
    when = when or datetime.now()
    date_str = when.strftime("%Y%m%d")
    hour_str = when.strftime("%H")
    return Path(base_dir) / date_str / hour_str


def ingest_book(book_id: int, base_dir: str = "datalake") -> bool:
    """
    Downloads a single book and stores it in the datalake using the
    time-based hierarchy (organized by the date/hour of ingestion).
    """
    output_dir = time_based_path(base_dir)
    return download_book(book_id, str(output_dir))


def ingest_books(book_ids, base_dir: str = "datalake") -> dict:
    """
    Downloads and stores multiple books. Returns a summary dict with
    the ids that succeeded and the ids that failed.
    """
    results = {"success": [], "failed": []}
    for book_id in book_ids:
        ok = ingest_book(book_id, base_dir)
        (results["success"] if ok else results["failed"]).append(book_id)
    return results


if __name__ == "__main__":
    import sys

    ids = [int(x) for x in sys.argv[1:]] or [1342]
    summary = ingest_books(ids)
    print(f"Ingested successfully: {summary['success']}")
    if summary["failed"]:
        print(f"Failed: {summary['failed']}")
