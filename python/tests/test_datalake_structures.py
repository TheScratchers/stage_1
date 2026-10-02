"""
Tests for the 3 datalake organization structures: datalake.py
(time-based), datalake_book.py (book-based) and datalake_batch.py
(batch/range-based).

Each module's ingest_book()/ingest_books() call download_book()
internally, which would normally hit the real network - so these
tests monkeypatch each module's own imported reference to
download_book with a fake that just records what it was asked to do,
the same way test_control.py fakes out its dependencies.
"""

from datetime import datetime
from pathlib import Path

import datalake
import datalake_book
import datalake_batch


# ---- time-based structure (datalake.py) ----

def test_time_based_path_uses_date_and_hour():
    when = datetime(2026, 1, 5, 14, 30)
    path = datalake.time_based_path("datalake", when)
    assert path == Path("datalake") / "20260105" / "14"


def test_ingest_book_time_based_calls_download_book_with_correct_path(monkeypatch):
    calls = []
    monkeypatch.setattr(
        datalake, "download_book", lambda book_id, output_path: calls.append((book_id, output_path)) or True
    )

    ok = datalake.ingest_book(1342, base_dir="my_datalake")

    assert ok is True
    assert len(calls) == 1
    book_id, output_path = calls[0]
    assert book_id == 1342
    assert output_path.startswith("my_datalake")


def test_ingest_books_splits_success_and_failed(monkeypatch):
    # Book 2 "fails to download", the other two succeed.
    monkeypatch.setattr(
        datalake, "download_book", lambda book_id, output_path: book_id != 2
    )

    summary = datalake.ingest_books([1, 2, 3], base_dir="x")

    assert summary["success"] == [1, 3]
    assert summary["failed"] == [2]


# ---- book-based structure (datalake_book.py) ----

def test_book_based_path_is_one_directory_per_book():
    assert datalake_book.book_based_path("datalake_book", 1342) == Path("datalake_book") / "1342"


def test_ingest_book_book_based(monkeypatch):
    calls = []
    monkeypatch.setattr(
        datalake_book, "download_book", lambda book_id, output_path: calls.append(output_path) or True
    )

    datalake_book.ingest_book(5, base_dir="dlb")

    assert calls == [str(Path("dlb") / "5")]


# ---- batch-based structure (datalake_batch.py) ----

def test_batch_based_path_groups_by_range():
    # With the default batch_size=1000: 0-999, 1000-1999, ...
    assert datalake_batch.batch_based_path("dl", 0) == Path("dl") / "batch_0-999"
    assert datalake_batch.batch_based_path("dl", 999) == Path("dl") / "batch_0-999"
    assert datalake_batch.batch_based_path("dl", 1000) == Path("dl") / "batch_1000-1999"
    assert datalake_batch.batch_based_path("dl", 2500) == Path("dl") / "batch_2000-2999"


def test_batch_based_path_respects_custom_batch_size():
    path = datalake_batch.batch_based_path("dl", 250, batch_size=100)
    assert path == Path("dl") / "batch_200-299"


def test_ingest_books_batch_based_reports_failures(monkeypatch):
    monkeypatch.setattr(
        datalake_batch, "download_book", lambda book_id, output_path: book_id % 2 == 0
    )

    summary = datalake_batch.ingest_books([10, 11, 12], base_dir="dlb")

    assert summary["success"] == [10, 12]
    assert summary["failed"] == [11]
