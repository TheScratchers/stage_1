"""Regression test for the contract's position-based replication rule
(shared/CONTRACT.md Section 4.1) in benchmark_metadata.py."""

import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))

import benchmark_metadata as bm  # noqa: E402

N_BOOKS = 25


def test_rows_replicate_real_books_by_position_not_by_id(tmp_path, monkeypatch):
    monkeypatch.setattr(bm, "BENCH_ROOT", tmp_path)
    # A start id that is NOT a multiple of 20: an id-based mapping
    # (book_id % 20) would shift every row; the position rule must not.
    start_id = 500003
    monkeypatch.setattr(
        bm, "make_synthetic_ids", lambda n, start_id=start_id: list(range(start_id, start_id + n))
    )
    meta_by_real_id = {
        real_id: {"title": f"Title {real_id}", "author": f"Author {real_id}", "language": "English"}
        for real_id in bm.REAL_BOOK_IDS
    }

    result = bm.benchmark_scale(N_BOOKS, meta_by_real_id)

    conn = sqlite3.connect(str(tmp_path / f"metadata_{N_BOOKS}.db"))
    try:
        for i in range(N_BOOKS):
            row = conn.execute(
                "SELECT author FROM books WHERE book_id = ?", (start_id + i,)
            ).fetchone()
            assert row == (f"Author {bm.REAL_BOOK_IDS[i % len(bm.REAL_BOOK_IDS)]}",)
    finally:
        conn.close()
    assert result["n_books"] == N_BOOKS
