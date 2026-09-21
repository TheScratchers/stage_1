"""
Stage 1 - Datalake Structure Benchmark: Batch/range-based hierarchy

Books are grouped into folders by ranges of book_id, reducing the
number of files stored in a single directory:

    <base_dir>/batch_0-999/<BOOK_ID>.body.txt
    <base_dir>/batch_1000-1999/<BOOK_ID>.body.txt
"""

from pathlib import Path

from download_book import download_book

DEFAULT_BATCH_SIZE = 1000


def batch_based_path(base_dir: str, book_id: int, batch_size: int = DEFAULT_BATCH_SIZE) -> Path:
    """
    Returns the batch directory a book_id belongs to:
    <base_dir>/batch_<start>-<end>
    """
    start = (book_id // batch_size) * batch_size
    end = start + batch_size - 1
    return Path(base_dir) / f"batch_{start}-{end}"


def ingest_book(book_id: int, base_dir: str = "datalake_batch", batch_size: int = DEFAULT_BATCH_SIZE) -> bool:
    """Downloads a single book and stores it under its batch directory."""
    output_dir = batch_based_path(base_dir, book_id, batch_size)
    return download_book(book_id, str(output_dir))


def ingest_books(book_ids, base_dir: str = "datalake_batch", batch_size: int = DEFAULT_BATCH_SIZE) -> dict:
    """Downloads and stores multiple books. Returns a success/failed summary."""
    results = {"success": [], "failed": []}
    for book_id in book_ids:
        ok = ingest_book(book_id, base_dir, batch_size)
        (results["success"] if ok else results["failed"]).append(book_id)
    return results


if __name__ == "__main__":
    import sys

    ids = [int(x) for x in sys.argv[1:]] or [1342]
    summary = ingest_books(ids)
    print(f"Ingested successfully: {summary['success']}")
    if summary["failed"]:
        print(f"Failed: {summary['failed']}")
