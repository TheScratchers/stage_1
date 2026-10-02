"""
Tests for download_book.py: the module that turns raw Gutenberg text
into the header/body files every other module builds on.

No real network calls are made here - download_book() is tested by
faking (monkeypatching) requests.get, so the tests run instantly and
don't depend on gutenberg.org being reachable.
"""

import download_book as db


class FakeResponse:
    """Minimal stand-in for a requests.Response, just enough for download_book()."""

    def __init__(self, text, status_code=200):
        self.text = text
        self.status_code = status_code

    def raise_for_status(self):
        if self.status_code >= 400:
            import requests
            raise requests.HTTPError(f"HTTP {self.status_code}")


def make_fake_gutenberg_text(header="HEADER STUFF", body="BODY STUFF", footer="FOOTER STUFF"):
    """Builds a fake Gutenberg-style text with the real start/end markers."""
    return f"{header}\n{db.START_MARKER}\n{body}\n{db.END_MARKER}\n{footer}"


def test_save_book_writes_header_and_body_files(tmp_path):
    db.save_book(book_id=42, header="  Some header  ", body="  Some body  ", output_path=str(tmp_path))

    body_file = tmp_path / "42.body.txt"
    header_file = tmp_path / "42.header.txt"

    assert body_file.exists()
    assert header_file.exists()
    # .strip() in save_book() should remove the surrounding whitespace.
    assert body_file.read_text(encoding="utf-8") == "Some body"
    assert header_file.read_text(encoding="utf-8") == "Some header"


def test_save_book_creates_missing_parent_directories(tmp_path):
    nested_dir = tmp_path / "does" / "not" / "exist" / "yet"
    db.save_book(book_id=1, header="h", body="b", output_path=str(nested_dir))

    assert (nested_dir / "1.body.txt").exists()


def test_download_book_success(tmp_path, monkeypatch):
    fake_text = make_fake_gutenberg_text(header="HEADER", body="THE BOOK TEXT")

    def fake_get(url, timeout=30):
        return FakeResponse(fake_text)

    monkeypatch.setattr(db.requests, "get", fake_get)

    ok = db.download_book(book_id=123, output_path=str(tmp_path))

    assert ok is True
    assert (tmp_path / "123.body.txt").read_text(encoding="utf-8") == "THE BOOK TEXT"
    assert (tmp_path / "123.header.txt").read_text(encoding="utf-8") == "HEADER"


def test_download_book_missing_markers_returns_false(tmp_path, monkeypatch):
    # No START/END markers at all in this "book" - simulates a
    # Gutenberg text that doesn't follow the expected format.
    monkeypatch.setattr(db.requests, "get", lambda url, timeout=30: FakeResponse("just some random text"))

    ok = db.download_book(book_id=999, output_path=str(tmp_path))

    assert ok is False
    # Nothing should have been written for a failed parse.
    assert not (tmp_path / "999.body.txt").exists()


def test_download_book_http_error_returns_false(tmp_path, monkeypatch):
    monkeypatch.setattr(db.requests, "get", lambda url, timeout=30: FakeResponse("", status_code=404))

    ok = db.download_book(book_id=1, output_path=str(tmp_path))

    assert ok is False


def test_download_book_network_exception_returns_false(tmp_path, monkeypatch):
    import requests

    def raise_connection_error(url, timeout=30):
        raise requests.ConnectionError("no network")

    monkeypatch.setattr(db.requests, "get", raise_connection_error)

    ok = db.download_book(book_id=1, output_path=str(tmp_path))

    assert ok is False
