"""
Tests for download_shared_dataset.py: the one-time setup script every
benchmark script depends on indirectly (it populates datalake_shared/,
the fixed folder the shared contract's 20 books must always be found
in). No real network calls are made - download_book() is replaced
with a fake, the same approach test_download_book.py already uses for
the lower-level module this one builds on.
"""

import download_shared_dataset as dsd


def test_read_book_ids_parses_ids_ignoring_comments_and_blanks(tmp_path):
    f = tmp_path / "books.txt"
    f.write_text(
        "# book_id - Title, Author\n"
        "# 11 | Alice's Adventures in Wonderland\n"
        "11\n"
        "\n"
        "84\n"
        "   \n"
        "1342\n",
        encoding="utf-8",
    )

    assert dsd.read_book_ids(f) == [11, 84, 1342]


def test_read_book_ids_converts_entries_to_int(tmp_path):
    f = tmp_path / "books.txt"
    f.write_text("11\n", encoding="utf-8")

    ids = dsd.read_book_ids(f)

    assert ids == [11]
    assert isinstance(ids[0], int)


def test_already_downloaded_true_only_when_both_files_exist(tmp_path):
    (tmp_path / "11.header.txt").write_text("h", encoding="utf-8")
    (tmp_path / "11.body.txt").write_text("b", encoding="utf-8")

    assert dsd.already_downloaded(11, tmp_path) is True


def test_already_downloaded_false_when_only_header_exists(tmp_path):
    # Simulates a partial/interrupted previous download (e.g. network
    # dropped mid-write) - must NOT be mistaken for a complete one and
    # silently skipped, or that book would be missing its body forever.
    (tmp_path / "12.header.txt").write_text("h", encoding="utf-8")

    assert dsd.already_downloaded(12, tmp_path) is False


def test_already_downloaded_false_when_only_body_exists(tmp_path):
    (tmp_path / "13.body.txt").write_text("b", encoding="utf-8")

    assert dsd.already_downloaded(13, tmp_path) is False


def test_already_downloaded_false_when_neither_file_exists(tmp_path):
    assert dsd.already_downloaded(14, tmp_path) is False


def test_main_skips_existing_downloads_and_fetches_only_missing_ones(tmp_path, monkeypatch, capsys):
    # End-to-end check of main()'s own orchestration logic (not just
    # the two helpers above in isolation): given a books.txt with 3
    # ids where one is already fully downloaded, one needs downloading
    # (and succeeds), and one needs downloading (and fails), main()
    # must skip the first, call download_book() for the other two, and
    # report each in the right bucket - exactly the logic that makes
    # re-running this script after a partial failure only retry what's
    # still missing.
    books_file = tmp_path / "books.txt"
    books_file.write_text("11\n84\n1342\n", encoding="utf-8")
    output_dir = tmp_path / "datalake_shared"
    output_dir.mkdir()

    # Book 11 already fully present - must be skipped, not re-downloaded.
    (output_dir / "11.header.txt").write_text("h", encoding="utf-8")
    (output_dir / "11.body.txt").write_text("b", encoding="utf-8")

    monkeypatch.setattr(dsd, "BOOKS_LIST", books_file)
    monkeypatch.setattr(dsd, "OUTPUT_DIR", output_dir)

    calls = []

    def fake_download_book(book_id, output_path):
        calls.append(book_id)
        # 84 "succeeds", 1342 "fails" (e.g. a network error or a
        # header/footer markers mismatch) - picked arbitrarily to
        # exercise both branches of main()'s if ok: / else: split.
        return book_id == 84

    monkeypatch.setattr(dsd, "download_book", fake_download_book)

    dsd.main()

    # download_book() is only ever called for the 2 ids NOT already
    # downloaded - book 11 must never be re-fetched.
    assert calls == [84, 1342]

    summary = capsys.readouterr().out
    assert "Already had:  1  [11]" in summary
    assert "Downloaded:   1  [84]" in summary
    assert "Failed:       1  [1342]" in summary
