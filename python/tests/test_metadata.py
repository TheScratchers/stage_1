"""
Tests for metadata.py: parsing Gutenberg headers and storing/querying
book metadata in SQLite.
"""

import metadata as md


SAMPLE_HEADER = """\
The Project Gutenberg eBook of Moby Dick
Title: Moby Dick
Author: Herman Melville
Language: English
Release date: January 1, 2001
"""


def test_parse_header_extracts_known_fields():
    result = md.parse_header(SAMPLE_HEADER)
    assert result == {
        "title": "Moby Dick",
        "author": "Herman Melville",
        "language": "English",
    }


def test_parse_header_missing_field_is_none():
    header_without_language = "Title: Some Book\nAuthor: Some Author\n"
    result = md.parse_header(header_without_language)
    assert result["title"] == "Some Book"
    assert result["author"] == "Some Author"
    assert result["language"] is None


def test_init_db_is_safe_to_call_twice(tmp_path):
    db_path = str(tmp_path / "books.db")
    md.init_db(db_path)
    md.init_db(db_path)  # must not raise


def test_upsert_book_then_find_by_id(tmp_path):
    db_path = str(tmp_path / "books.db")
    md.init_db(db_path)

    md.upsert_book(db_path, 1342, "Moby Dick", "Herman Melville", "English", "body.txt", "header.txt")

    row = md.find_by_id(db_path, 1342)
    assert row is not None
    # Column order matches the CREATE TABLE statement in init_db().
    assert row[0] == 1342
    assert row[1] == "Moby Dick"
    assert row[2] == "Herman Melville"


def test_upsert_book_updates_existing_row_instead_of_duplicating(tmp_path):
    db_path = str(tmp_path / "books.db")
    md.init_db(db_path)

    md.upsert_book(db_path, 1, "First Title", "Author A", "English", "b", "h")
    md.upsert_book(db_path, 1, "Updated Title", "Author A", "English", "b", "h")

    row = md.find_by_id(db_path, 1)
    assert row[1] == "Updated Title"


def test_find_by_id_returns_none_when_missing(tmp_path):
    db_path = str(tmp_path / "books.db")
    md.init_db(db_path)
    assert md.find_by_id(db_path, 999999) is None


def test_find_by_author_matches_substring_case_insensitively(tmp_path):
    db_path = str(tmp_path / "books.db")
    md.init_db(db_path)
    md.upsert_book(db_path, 1, "Pride and Prejudice", "Jane Austen", "English", "b", "h")
    md.upsert_book(db_path, 2, "Emma", "Jane Austen", "English", "b", "h")
    md.upsert_book(db_path, 3, "Moby Dick", "Herman Melville", "English", "b", "h")

    rows = md.find_by_author(db_path, "austen")

    assert {row[0] for row in rows} == {1, 2}


def test_index_metadata_scans_datalake_and_fills_db(tmp_path):
    datalake_dir = tmp_path / "datalake"
    (datalake_dir / "20260101" / "10").mkdir(parents=True)

    header_file = datalake_dir / "20260101" / "10" / "1342.header.txt"
    header_file.write_text(SAMPLE_HEADER, encoding="utf-8")
    body_file = datalake_dir / "20260101" / "10" / "1342.body.txt"
    body_file.write_text("Call me Ishmael.", encoding="utf-8")

    db_path = str(tmp_path / "books.db")
    count = md.index_metadata(str(datalake_dir), db_path)

    assert count == 1
    row = md.find_by_id(db_path, 1342)
    assert row[1] == "Moby Dick"
    # body_path/header_path should point at the actual files found.
    assert row[4].endswith("1342.body.txt")  # column order: book_id, title, author, language, body_path, header_path
