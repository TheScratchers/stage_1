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
    for field, pattern in FIELD_PATTERNS.items():
        match = pattern.search(header_text)
        result[field] = match.group(1).strip() if match else None
    return result


def init_db(db_path: str) -> None:
    """Creates the books table if it doesn't already exist."""
    Path(db_path).parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(db_path)
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
    init_db(db_path)
    count = 0
    for header_file in sorted(Path(datalake_dir).rglob("*.header.txt")):
        book_id = int(header_file.name.split(".")[0])
        header_text = header_file.read_text(encoding="utf-8")
        meta = parse_header(header_text)
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
    rows = conn.execute(
        "SELECT book_id, title, author, language FROM books WHERE author LIKE ?",
        (f"%{author}%",),
    ).fetchall()
    conn.close()
    return rows


def find_by_id(db_path: str, book_id: int):
    """Returns the full row (including file paths) for a single book_id."""
    conn = sqlite3.connect(db_path)
    row = conn.execute(
        "SELECT * FROM books WHERE book_id = ?", (book_id,)
    ).fetchone()
    conn.close()
    return row


if __name__ == "__main__":
    import sys

    datalake_dir = sys.argv[1] if len(sys.argv) > 1 else "datalake"
    db_path = sys.argv[2] if len(sys.argv) > 2 else "datamarts/books.db"

    n = index_metadata(datalake_dir, db_path)
    print(f"Indexed metadata for {n} book(s) into {db_path}")
