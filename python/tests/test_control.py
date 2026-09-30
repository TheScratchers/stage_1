"""
Tests for control.py: the orchestrator that decides, on each step,
whether to index a pending book or download a new one.

control.py works entirely with paths *relative* to the current
working directory (e.g. Path("control"), "datamarts/books.db"). So
instead of monkeypatching each of those constants individually - which
does NOT work for functions like _find_book_files(), whose default
argument value is bound once when the module is first imported, not
re-read on every call - these tests use monkeypatch.chdir(tmp_path) to
make every one of those relative paths land inside a fresh temporary
folder. This is also exactly how control.py is actually meant to be
run (from the project's python/ folder).
"""

import control


def test_read_ids_returns_empty_set_when_file_missing(tmp_path):
    assert control._read_ids(tmp_path / "nope.txt") == set()


def test_append_id_then_read_ids_roundtrip(tmp_path):
    path = tmp_path / "control" / "ids.txt"
    control._append_id(path, 5)
    control._append_id(path, 11)

    assert control._read_ids(path) == {5, 11}


def test_find_book_files_locates_files_anywhere_in_datalake(tmp_path):
    book_dir = tmp_path / "datalake" / "20260101" / "10"
    book_dir.mkdir(parents=True)
    (book_dir / "5.body.txt").write_text("body", encoding="utf-8")
    (book_dir / "5.header.txt").write_text("header", encoding="utf-8")

    body, header = control._find_book_files(5, datalake_dir=str(tmp_path / "datalake"))

    assert body.name == "5.body.txt"
    assert header.name == "5.header.txt"


def test_find_book_files_returns_none_when_missing(tmp_path):
    (tmp_path / "datalake").mkdir()
    body, header = control._find_book_files(999, datalake_dir=str(tmp_path / "datalake"))
    assert body is None
    assert header is None


def test_control_pipeline_step_downloads_when_nothing_pending(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(control.random, "randint", lambda a, b: 42)
    monkeypatch.setattr(control, "ingest_book", lambda book_id, base_dir: True)

    control.control_pipeline_step()

    assert control._read_ids(control.DOWNLOADED) == {42}
    assert control._read_ids(control.INDEXED) == set()


def test_control_pipeline_step_does_not_record_failed_download(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(control.random, "randint", lambda a, b: 42)
    monkeypatch.setattr(control, "ingest_book", lambda book_id, base_dir: False)

    control.control_pipeline_step()

    assert control._read_ids(control.DOWNLOADED) == set()


def test_control_pipeline_step_skips_already_downloaded_candidates(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    # Book 7 is already downloaded AND indexed, so it's not pending
    # indexing - the step should go straight to the download branch,
    # skip candidate 7 there too, and land on 42.
    control._append_id(control.DOWNLOADED, 7)
    control._append_id(control.INDEXED, 7)

    ids_to_try = iter([7, 42])
    monkeypatch.setattr(control.random, "randint", lambda a, b: next(ids_to_try))
    monkeypatch.setattr(control, "ingest_book", lambda book_id, base_dir: True)

    control.control_pipeline_step()

    assert control._read_ids(control.DOWNLOADED) == {7, 42}


def test_control_pipeline_step_indexes_before_downloading(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    # Book 5 is downloaded but not indexed yet.
    control._append_id(control.DOWNLOADED, 5)

    index_calls = []
    monkeypatch.setattr(control, "index_book", lambda book_id: index_calls.append(book_id) or True)

    def fail_if_called(*a, **kw):
        raise AssertionError("ingest_book should not run while a book is pending indexing")

    monkeypatch.setattr(control, "ingest_book", fail_if_called)

    control.control_pipeline_step()

    assert index_calls == [5]
    assert control._read_ids(control.INDEXED) == {5}


def test_control_pipeline_step_picks_smallest_pending_id_first(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    control._append_id(control.DOWNLOADED, 99)
    control._append_id(control.DOWNLOADED, 3)
    control._append_id(control.DOWNLOADED, 42)

    index_calls = []
    monkeypatch.setattr(control, "index_book", lambda book_id: index_calls.append(book_id) or True)

    control.control_pipeline_step()

    assert index_calls == [3]


def test_index_book_updates_metadata_and_both_indexes(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    book_dir = tmp_path / "datalake" / "5"
    book_dir.mkdir(parents=True)
    (book_dir / "5.body.txt").write_text("whale rabbit", encoding="utf-8")
    (book_dir / "5.header.txt").write_text("Title: Moby Dick\nAuthor: Herman Melville\n", encoding="utf-8")

    ok = control.index_book(5)

    assert ok is True

    import metadata
    row = metadata.find_by_id(control.DB_PATH, 5)
    assert row[1] == "Moby Dick"

    import inverted_index
    json_index = inverted_index.load_index(control.JSON_INDEX_PATH)
    assert json_index["whale"] == [5]

    import inverted_index_hierarchical as iih
    assert iih.search("rabbit", control.HIER_INDEX_ROOT) == [5]


def test_index_book_returns_false_when_book_not_in_datalake(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    (tmp_path / "datalake").mkdir()

    assert control.index_book(12345) is False
