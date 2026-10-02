"""
Tests for inverted_index.py: the monolithic single-JSON-file inverted
index structure.
"""

import inverted_index as ii


def test_tokenize_lowercases_and_keeps_only_letters():
    assert ii.tokenize("The Whale, the Sea! 1851") == ["the", "whale", "the", "sea"]


def test_build_index_from_datalake(tmp_path):
    datalake_dir = tmp_path / "datalake"
    book_dir = datalake_dir / "5"
    book_dir.mkdir(parents=True)
    (book_dir / "5.body.txt").write_text("the whale swims", encoding="utf-8")

    other_dir = datalake_dir / "11"
    other_dir.mkdir(parents=True)
    (other_dir / "11.body.txt").write_text("the rabbit runs", encoding="utf-8")

    index = ii.build_index(str(datalake_dir))

    assert index["the"] == [5, 11]
    assert index["whale"] == [5]
    assert index["rabbit"] == [11]


def test_save_and_load_index_roundtrip(tmp_path):
    index = {"whale": [5, 42], "rabbit": [11]}
    path = str(tmp_path / "datamarts" / "inverted_index.json")

    ii.save_index(index, path)
    loaded = ii.load_index(path)

    assert loaded == index


def test_update_index_adds_new_book_without_duplicating():
    index = {"whale": [5]}

    ii.update_index(index, book_id=42, body_text="the whale swims")
    # Running the exact same update again (e.g. control.py re-run)
    # must not create a duplicate entry.
    ii.update_index(index, book_id=42, body_text="the whale swims")

    assert index["whale"] == [5, 42]


def test_update_index_keeps_postings_sorted():
    index = {}
    ii.update_index(index, book_id=99, body_text="whale")
    ii.update_index(index, book_id=3, body_text="whale")

    assert index["whale"] == [3, 99]


def test_search_returns_empty_list_for_unknown_term():
    index = {"whale": [5]}
    assert ii.search(index, "unknownterm") == []


def test_search_is_case_insensitive():
    index = {"whale": [5]}
    assert ii.search(index, "WHALE") == [5]
