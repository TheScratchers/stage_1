"""
Smoke test for benchmark_datalake.benchmark_structure() at a tiny scale.

The real benchmark takes a long time at 10,000 books, so it is never run
from the test suite. But a typo in the code that builds its result dict
(for example a variable renamed in one place and not in another) would
only show up at the very end of the first structure of a real run, after
the first writes, lookups and the recovery simulation had already
executed. This test runs the same function with 25 synthetic books and
tiny fake content in a temporary folder, so that kind of mistake fails
here in a fraction of a second instead.

It also pins down the cross-language replication rule (CONTRACT.md,
Section 4.1): the i-th synthetic book (0-indexed position) must hold the
content of the real book at position i mod 20 of books.txt.
"""

from pathlib import Path

import pytest

import benchmark_datalake as bd
from datalake import time_based_path
from datalake_book import book_based_path
from datalake_batch import batch_based_path

N_BOOKS = 25  # not a multiple of 20 on purpose: exercises the wrap-around

STRUCTURES = [
    ("time_based", lambda base, book_id: time_based_path(base)),
    ("book_based", book_based_path),
    ("batch_based", batch_based_path),
]


def _fake_content():
    # One tiny header/body per real contract book, distinguishable by id.
    return {rid: (f"HEADER {rid}", f"BODY {rid}") for rid in bd.REAL_BOOK_IDS}


@pytest.mark.parametrize("name,path_fn", STRUCTURES)
def test_benchmark_structure_runs_and_reports_consistent_numbers(
    tmp_path, monkeypatch, name, path_fn
):
    monkeypatch.setattr(bd, "BENCH_ROOT", tmp_path)
    book_ids = bd.make_synthetic_ids(N_BOOKS)

    result = bd.benchmark_structure(name, path_fn, book_ids, _fake_content())

    assert result["structure"] == name
    assert result["n_books"] == N_BOOKS
    # The incremental step offers the second half of the existing ids plus
    # N_BOOKS // 10 genuinely new ones; only the new ones must be detected.
    assert result["incremental_new_found"] == max(1, N_BOOKS // 10)
    # Recovery: the first half survives the simulated crash, the rest is
    # written on resume, and nothing is lost or duplicated.
    assert result["recovery_resumed_count"] == N_BOOKS - N_BOOKS // 2
    assert result["recovery_ok"] is True


@pytest.mark.parametrize("name,path_fn", STRUCTURES)
def test_benchmark_structure_uses_position_based_content(
    tmp_path, monkeypatch, name, path_fn
):
    monkeypatch.setattr(bd, "BENCH_ROOT", tmp_path)
    book_ids = bd.make_synthetic_ids(N_BOOKS)

    bd.benchmark_structure(name, path_fn, book_ids, _fake_content())

    # After the recovery step the structure holds all N_BOOKS books again.
    base_dir = tmp_path / name
    for i, book_id in enumerate(book_ids):
        expected_real_id = bd.REAL_BOOK_IDS[i % len(bd.REAL_BOOK_IDS)]
        body_path = Path(path_fn(str(base_dir), book_id)) / f"{book_id}.body.txt"
        assert body_path.read_text(encoding="utf-8").strip() == f"BODY {expected_real_id}"
