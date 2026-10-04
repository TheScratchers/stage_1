"""
Tests for shared_contract.py: the single place that parses the
cross-language benchmark contract's companion files (books.txt,
words.txt) for all 3 Python benchmark scripts.

This module matters more than its 47 lines suggest - a parsing bug
here (wrong order, a dropped comment line treated as a book id, a
word not lowercased) would silently make every Python benchmark
incomparable with the Java/C++ results, which is the entire point of
having a shared contract in the first place. No network or real
shared/ files are needed: _read_lines() is tested directly against
tmp_path fixtures, and load_book_ids()/load_words() are tested by
monkeypatching the module's BOOKS_FILE/WORDS_FILE constants to point
at those fixtures instead of the real ../shared/ files.
"""

import shared_contract as sc


def test_read_lines_parses_plain_entries(tmp_path):
    f = tmp_path / "plain.txt"
    f.write_text("11\n84\n1342\n", encoding="utf-8")

    assert sc._read_lines(f) == ["11", "84", "1342"]


def test_read_lines_skips_blank_lines_and_comments(tmp_path):
    f = tmp_path / "with_comments.txt"
    f.write_text(
        "# Stage 1 - shared books\n"
        "11\n"
        "\n"
        "   \n"
        "# 84 - some commented-out book, not actually included\n"
        "84\n",
        encoding="utf-8",
    )

    assert sc._read_lines(f) == ["11", "84"]


def test_read_lines_strips_surrounding_whitespace(tmp_path):
    f = tmp_path / "whitespace.txt"
    f.write_text("  11  \n\tdeath\t\n", encoding="utf-8")

    assert sc._read_lines(f) == ["11", "death"]


def test_read_lines_preserves_file_order(tmp_path):
    # Order matters: load_book_ids()/load_words() are used to build
    # query workloads and synthetic id ranges whose order other code
    # (and the written-up report) depends on - _read_lines() must
    # never reorder or deduplicate what's in the file.
    f = tmp_path / "ordered.txt"
    f.write_text("monster\ntime\nmonster\nlove\n", encoding="utf-8")

    assert sc._read_lines(f) == ["monster", "time", "monster", "love"]


def test_load_book_ids_returns_ints_in_file_order(tmp_path, monkeypatch):
    books_file = tmp_path / "books.txt"
    books_file.write_text(
        "# book_id - Title, Author\n"
        "# 1342 | Pride and Prejudice | Jane Austen\n"
        "1342\n"
        "# 11 | Alice's Adventures in Wonderland | Lewis Carroll\n"
        "11\n"
        "\n"
        "# 84 | Frankenstein | Mary Shelley\n"
        "84\n",
        encoding="utf-8",
    )
    monkeypatch.setattr(sc, "BOOKS_FILE", books_file)

    book_ids = sc.load_book_ids()

    # Every entry converted to int (not left as str), in the exact
    # order they appear in the file.
    assert book_ids == [1342, 11, 84]
    assert all(isinstance(book_id, int) for book_id in book_ids)


def test_load_book_ids_rejects_non_comment_non_numeric_lines(tmp_path, monkeypatch):
    # A malformed books.txt (e.g. a stray title line not marked with
    # "#") should fail loudly with a ValueError from int(), rather
    # than silently producing a wrong id - better to crash a benchmark
    # run than to quietly compare against the wrong dataset.
    books_file = tmp_path / "books.txt"
    books_file.write_text("1342\nPride and Prejudice\n", encoding="utf-8")
    monkeypatch.setattr(sc, "BOOKS_FILE", books_file)

    try:
        sc.load_book_ids()
        assert False, "expected a ValueError for the non-numeric line"
    except ValueError:
        pass


def test_load_words_returns_strings_in_tier_order(tmp_path, monkeypatch):
    words_file = tmp_path / "words.txt"
    words_file.write_text(
        "# common\n"
        "time\n"
        "love\n"
        "# rare\n"
        "wonderland\n"
        "darcy\n",
        encoding="utf-8",
    )
    monkeypatch.setattr(sc, "WORDS_FILE", words_file)

    words = sc.load_words()

    assert words == ["time", "love", "wonderland", "darcy"]
    assert all(isinstance(word, str) for word in words)


def test_metadata_author_constants_are_the_two_fixed_contract_authors():
    # These 2 names are agreed directly in CONTRACT.md's text (there's
    # no authors.txt), so there's no file-parsing to test here - just
    # a guard against either constant being accidentally edited to a
    # different author, which would silently make the metadata
    # benchmark's repeated-vs-unique comparison meaningless.
    assert sc.METADATA_REPEATED_AUTHOR == "Charles Dickens"
    assert sc.METADATA_UNIQUE_AUTHOR == "Lewis Carroll"
