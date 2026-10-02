"""
Stage 1 - Datamarts: Metadata

Parses book metadata (Title, Author, Language) from the header files
produced by download_book, and stores it in a SQLite database so it
can be queried without scanning the full text files.

Schema (books table):
    book_id     INTEGER PRIMARY KEY
    title       TEXT
    author      TEXT
    language    TEXT
    body_path   TEXT   -- path to the cleaned body file in the datalake
    header_path TEXT   -- path to the header file in the datalake
"""

import re
import sqlite3
from pathlib import Path

# re.MULTILINE lets "^" match the start of each line in the header
# (not just the start of the whole string), since these fields can
# appear anywhere in a multi-line Gutenberg header block.
FIELD_PATTERNS = {
    "title": re.compile(r"^Title:\s*(.+)$", re.MULTILINE),
    "author": re.compile(r"^Author:\s*(.+)$", re.MULTILINE),
    "language": re.compile(r"^Language:\s*(.+)$", re.MULTILINE),
}


def parse_header(header_text: str) -> dict:
    """
    Extracts title, author and language from a Project Gutenberg header
    using regex. Missing fields are returned as None.
    """
    result = {}
    # Try each field's pattern against the whole header text; if it
    # doesn't match (field missing/renamed in this particular book),
    # store None instead of raising an error.
    for field, pattern in FIELD_PATTERNS.items():
        match = pattern.search(header_text)
        result[field] = match.group(1).strip() if match else None
    return result


def init_db(db_path: str) -> None:
    """Creates the books table if it doesn't already exist."""
    # Make sure the parent folder (e.g. datamarts/) exists before
    # sqlite3 tries to create the database file inside it.
    Path(db_path).parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(db_path)
    # IF NOT EXISTS makes this safe to call every time (e.g. once per
    # control_pipeline_step()) without erroring on a table that's
    # already there.
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS books (
            book_id     INTEGER PRIMARY KEY,
            title       TEXT,
            author      TEXT,
            language    TEXT,
            body_path   TEXT,
            header_path TEXT
        )
        """
    )
    conn.commit()
    conn.close()


def upsert_book(
    db_path: str,
    book_id: int,
    title: str,
    author: str,
    language: str,
    body_path: str,
    header_path: str,
) -> None:
    """Inserts a book's metadata, or updates it if the id already exists."""
    conn = sqlite3.connect(db_path)
    # ON CONFLICT ... DO UPDATE (upsert) means index_metadata / control.py
    # can be re-run safely over the same book_id without raising a
    # duplicate-key error or requiring a separate "does it exist?" check.
    # The "?" placeholders are filled in, in order, by the tuple passed
    # as the second argument - this also protects against SQL injection.
    conn.execute(
        """
        INSERT INTO books (book_id, title, author, language, body_path, header_path)
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(book_id) DO UPDATE SET
            title=excluded.title,
            author=excluded.author,
            language=excluded.language,
            body_path=excluded.body_path,
            header_path=excluded.header_path
        """,
        (book_id, title, author, language, body_path, header_path),
    )
    conn.commit()
    conn.close()


def index_metadata(datalake_dir: str, db_path: str = "datamarts/books.db") -> int:
    """
    Scans a datalake directory for *.header.txt files, parses each one,
    and stores the metadata in SQLite. Returns the number of books indexed.
    """
    # Make sure the table exists before we start inserting into it.
    init_db(db_path)
    count = 0
    # rglob searches recursively, so this works no matter which
    # datalake structure (time/book/batch-based) produced the files.
    for header_file in sorted(Path(datalake_dir).rglob("*.header.txt")):
        # The book_id is the part of the filename before the first
        # "." (e.g. "1342.header.txt" -> "1342" -> 1342).
        book_id = int(header_file.name.split(".")[0])
        header_text = header_file.read_text(encoding="utf-8")
        meta = parse_header(header_text)
        # Body file is assumed to sit next to its header (same
        # directory, same book_id prefix) - true for every datalake
        # structure in this project since download_book() writes both
        # files into the same output directory.
        body_file = header_file.parent / f"{book_id}.body.txt"

        upsert_book(
            db_path,
            book_id,
            meta["title"],
            meta["author"],
            meta["language"],
            str(body_file),
            str(header_file),
        )
        count += 1
    return count


def find_by_author(db_path: str, author: str) -> list:
    """Returns all books whose author matches (case-insensitive substring)."""
    conn = sqlite3.connect(db_path)
    # LIKE '%<author>%' matches the given text anywhere inside the
    # author column, so "Austen" also matches "Jane Austen".
    rows = conn.execute(
        "SELECT book_id, title, author, language FROM books WHERE author LIKE ?",
        (f"%{author}%",),
    ).fetchall()
    conn.close()
    return rows


def find_by_id(db_path: str, book_id: int):
    """Returns the full row (including file paths) for a single book_id."""
    conn = sqlite3.connect(db_path)
    # fetchone() returns a single row (or None if not found), since
    # book_id is the primary key and can match at most one row.
    row = conn.execute(
        "SELECT * FROM books WHERE book_id = ?", (book_id,)
    ).fetchone()
    conn.close()
    return row


if __name__ == "__main__":
    import sys

    # Command-line entry point: `python metadata.py <datalake_dir> <db_path>`,
    # falling back to sensible defaults so the script also works with no
    # arguments at all.
    datalake_dir = sys.argv[1] if len(sys.argv) > 1 else "datalake"
    db_path = sys.argv[2] if len(sys.argv) > 2 else "datamarts/books.db"

    n = index_metadata(datalake_dir, db_path)
    print(f"Indexed metadata for {n} book(s) into {db_path}")
