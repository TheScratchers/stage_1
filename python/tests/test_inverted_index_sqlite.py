"""
Tests for inverted_index_sqlite.py: a single SQLite database with one
(term, book_id) row per posting, mirroring Java's SqliteIndexStorage
schema.
"""

import sqlite3

import inverted_index_sqlite as iis


def test_connect_creates_table_with_composite_primary_key(tmp_path):
    db_path = str(tmp_path / "index.db")
    conn = iis._connect(db_path)
    try:
        cursor = conn.execute("PRAGMA table_info(inverted_index)")
        columns = {row[1] for row in cursor.fetchall()}
        assert columns == {"term", "book_id"}
    finally:
        conn.close()


def test_build_index_writes_one_row_per_posting(tmp_path):
    datalake_dir = tmp_path / "datalake"
    book_dir = datalake_dir / "5"
    book_dir.mkdir(parents=True)
    (book_dir / "5.body.txt").write_text("whale whale rabbit", encoding="utf-8")

    db_path = str(tmp_path / "index.db")
    n_terms = iis.build_index(str(datalake_dir), db_path)

    assert n_terms == 2
    assert iis.search("whale", db_path) == [5]
    assert iis.search("rabbit", db_path) == [5]


def test_build_index_is_a_clean_rebuild(tmp_path):
    # A second build_index() call (e.g. a re-run) must not leave stale
    # rows from a book that no longer exists in the datalake behind -
    # same "clean rebuild" guarantee the JSON and hierarchical
    # structures give via overwrite/fresh-folder semantics.
    datalake_dir = tmp_path / "datalake"
    book_dir = datalake_dir / "5"
    book_dir.mkdir(parents=True)
    (book_dir / "5.body.txt").write_text("whale", encoding="utf-8")

    db_path = str(tmp_path / "index.db")
    iis.build_index(str(datalake_dir), db_path)

    # Replace the one book in the datalake with a different one before
    # rebuilding.
    (book_dir / "5.body.txt").unlink()
    book_dir2 = datalake_dir / "11"
    book_dir2.mkdir()
    (book_dir2 / "11.body.txt").write_text("rabbit", encoding="utf-8")

    iis.build_index(str(datalake_dir), db_path)

    assert iis.search("whale", db_path) == []
    assert iis.search("rabbit", db_path) == [11]


def test_search_unknown_term_returns_empty_list(tmp_path):
    db_path = str(tmp_path / "index.db")
    assert iis.search("nosuchterm", db_path) == []


def test_search_is_case_insensitive(tmp_path):
    datalake_dir = tmp_path / "datalake"
    book_dir = datalake_dir / "5"
    book_dir.mkdir(parents=True)
    (book_dir / "5.body.txt").write_text("Whale", encoding="utf-8")

    db_path = str(tmp_path / "index.db")
    iis.build_index(str(datalake_dir), db_path)

    assert iis.search("WHALE", db_path) == [5]


def test_search_orders_book_ids_ascending(tmp_path):
    db_path = str(tmp_path / "index.db")
    iis.update_index(book_id=42, body_text="whale", db_path=db_path)
    iis.update_index(book_id=5, body_text="whale", db_path=db_path)
    iis.update_index(book_id=11, body_text="whale", db_path=db_path)

    assert iis.search("whale", db_path) == [5, 11, 42]


def test_update_index_only_touches_relevant_terms(tmp_path):
    db_path = str(tmp_path / "index.db")

    iis.update_index(book_id=5, body_text="whale", db_path=db_path)
    iis.update_index(book_id=11, body_text="rabbit", db_path=db_path)

    assert iis.search("whale", db_path) == [5]
    assert iis.search("rabbit", db_path) == [11]

    # Adding the same term again for a different book should add a
    # new row, not overwrite the existing one.
    iis.update_index(book_id=42, body_text="whale", db_path=db_path)
    assert iis.search("whale", db_path) == [5, 42]


def test_update_index_does_not_duplicate_same_book_id(tmp_path):
    db_path = str(tmp_path / "index.db")
    iis.update_index(book_id=5, body_text="whale", db_path=db_path)
    iis.update_index(book_id=5, body_text="whale", db_path=db_path)

    # INSERT OR IGNORE against the (term, book_id) primary key means a
    # repeat call is a no-op, not a second row.
    assert iis.search("whale", db_path) == [5]

    conn = sqlite3.connect(db_path)
    try:
        count = conn.execute(
            "SELECT COUNT(*) FROM inverted_index WHERE term = ? AND book_id = ?",
            ("whale", 5),
        ).fetchone()[0]
        assert count == 1
    finally:
        conn.close()
