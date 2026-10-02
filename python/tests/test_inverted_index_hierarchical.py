"""
Tests for inverted_index_hierarchical.py: one folder per starting
letter, one file per term.
"""

from pathlib import Path

import inverted_index_hierarchical as iih


def test_term_path_buckets_by_first_letter(tmp_path):
    path = iih._term_path("whale", str(tmp_path))
    assert path == Path(str(tmp_path)) / "W" / "whale.txt"


def test_term_path_falls_back_to_underscore_bucket_for_non_alpha(tmp_path):
    path = iih._term_path("", str(tmp_path))
    assert path == Path(str(tmp_path)) / "_" / ".txt"


def test_build_index_writes_one_file_per_term(tmp_path):
    datalake_dir = tmp_path / "datalake"
    book_dir = datalake_dir / "5"
    book_dir.mkdir(parents=True)
    (book_dir / "5.body.txt").write_text("whale whale rabbit", encoding="utf-8")

    index_root = tmp_path / "index"
    n_terms = iih.build_index(str(datalake_dir), str(index_root))

    assert n_terms == 2
    assert (index_root / "W" / "whale.txt").read_text(encoding="utf-8") == "5"
    assert (index_root / "R" / "rabbit.txt").read_text(encoding="utf-8") == "5"


def test_search_reads_matching_file(tmp_path):
    datalake_dir = tmp_path / "datalake"
    book_dir = datalake_dir / "5"
    book_dir.mkdir(parents=True)
    (book_dir / "5.body.txt").write_text("whale", encoding="utf-8")

    index_root = tmp_path / "index"
    iih.build_index(str(datalake_dir), str(index_root))

    assert iih.search("whale", str(index_root)) == [5]


def test_search_unknown_term_returns_empty_list(tmp_path):
    index_root = tmp_path / "index"
    index_root.mkdir()
    assert iih.search("nosuchterm", str(index_root)) == []


def test_update_index_only_touches_relevant_term_files(tmp_path):
    index_root = tmp_path / "index"

    iih.update_index(book_id=5, body_text="whale", index_root=str(index_root))
    iih.update_index(book_id=11, body_text="rabbit", index_root=str(index_root))

    assert iih.search("whale", str(index_root)) == [5]
    assert iih.search("rabbit", str(index_root)) == [11]

    # Adding the same term again for a different book should merge
    # into the existing file rather than overwrite it.
    iih.update_index(book_id=42, body_text="whale", index_root=str(index_root))
    assert iih.search("whale", str(index_root)) == [5, 42]


def test_update_index_does_not_duplicate_same_book_id(tmp_path):
    index_root = tmp_path / "index"
    iih.update_index(book_id=5, body_text="whale", index_root=str(index_root))
    iih.update_index(book_id=5, body_text="whale", index_root=str(index_root))

    assert iih.search("whale", str(index_root)) == [5]
