"""
Stage 1 - Datamarts: Inverted Index (SQLite structure)

Stores the inverted index as a single SQLite database with one table:

    CREATE TABLE inverted_index (
        term    TEXT NOT NULL,
        book_id INTEGER NOT NULL,
        PRIMARY KEY (term, book_id)
    )

One row per (term, book_id) pair, instead of one document/file per
term - the composite primary key both prevents duplicate rows for the
same book and term, and gives fast lookups via the index SQLite
automatically builds for a PRIMARY KEY.

This schema intentionally mirrors the Java implementation's
SqliteIndexStorage (datamarts/index/SqliteIndexStorage.java) rather
than reinventing one, since all three languages (Python, Java, C++)
agreed to use SQLite as the third inverted-index structure - the whole
point of sharing a structure across languages is that the on-disk
representation is comparable, not just the public API.

Trade-off: like the hierarchical structure, there's no "load the whole
index into memory" step - every lookup is a query, executed by
SQLite's own query engine against the on-disk B-tree index rather than
a Python dict or a single open file per term. Updates are a handful of
small INSERT OR IGNORE statements wrapped in one transaction, rather
than a full-file rewrite (JSON) or one-file-per-term rewrite
(hierarchical). See benchmark_inverted_index.py for a concrete
comparison.
"""

import re
import sqlite3
import sys
from collections import defaultdict
from pathlib import Path

TOKEN_PATTERN = re.compile(r"[A-Za-z]+")
DEFAULT_DB_PATH = "datamarts/inverted_index.db"


def tokenize(text: str) -> list:
    # .lower() first so "The" and "the" are treated as the same term;
    # findall() then returns every run of letters as a separate word.
    return TOKEN_PATTERN.findall(text.lower())


def _connect(db_path: str) -> sqlite3.Connection:
    # Make sure the parent folder (e.g. datamarts/) exists before
    # SQLite tries to create the .db file inside it.
    Path(db_path).parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(db_path)
    # Same (term, book_id) composite primary key as Java's
    # SqliteIndexStorage - this is what makes the two schemas (and
    # therefore the storage-overhead/lookup numbers) genuinely
    # comparable, not just superficially similar.
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS inverted_index (
            term    TEXT NOT NULL,
            book_id INTEGER NOT NULL,
            PRIMARY KEY (term, book_id)
        )
        """
    )
    conn.commit()
    return conn


def build_index(datalake_dir: str, db_path: str = DEFAULT_DB_PATH) -> int:
    """
    Builds the SQLite index from scratch by scanning every book in the
    datalake. Returns the number of unique terms written.
    """
    # First pass: build the whole index in memory (one set of book_ids
    # per term), the same way build_index() does in inverted_index.py
    # and inverted_index_hierarchical.py.
    postings = defaultdict(set)
    body_files = sorted(Path(datalake_dir).rglob("*.body.txt"))
    for i, body_file in enumerate(body_files):
        # The book_id is the part of the filename before the first
        # "." (e.g. "1342.body.txt" -> "1342" -> 1342).
        book_id = int(body_file.name.split(".")[0])
        text = body_file.read_text(encoding="utf-8")
        for term in set(tokenize(text)):
            postings[term].add(book_id)
        # Heartbeat for large scales: this loop alone can run for
        # minutes with no other output, which looks indistinguishable
        # from a hang. Every 200 books is frequent enough to reassure
        # without flooding the console.
        if (i + 1) % 200 == 0:
            print(f"    ...processed {i + 1}/{len(body_files)} books")

    conn = _connect(db_path)
    try:
        # Clean rebuild: drop whatever rows are already there, same as
        # inverted_index_mongo.py's delete_many({}) used to do, so a
        # second build_index() call never mixes stale and fresh rows.
        conn.execute("DELETE FROM inverted_index")
        rows = [
            (term, book_id)
            for term, book_ids in postings.items()
            for book_id in book_ids
        ]
        # executemany + a single commit: all rows go in inside one
        # transaction, instead of one commit per row.
        conn.executemany(
            "INSERT OR IGNORE INTO inverted_index (term, book_id) VALUES (?, ?)",
            rows,
        )
        conn.commit()
        return len(postings)
    finally:
        conn.close()


def search(term: str, db_path: str = DEFAULT_DB_PATH) -> list:
    """Looks up a single term via a SELECT against the SQLite table."""
    conn = _connect(db_path)
    try:
        cursor = conn.execute(
            "SELECT book_id FROM inverted_index WHERE term = ? ORDER BY book_id",
            (term.lower(),),
        )
        return [row[0] for row in cursor.fetchall()]
    finally:
        conn.close()


def update_index(book_id: int, body_text: str, db_path: str = DEFAULT_DB_PATH) -> None:
    """
    Incrementally adds one book to the index: one INSERT OR IGNORE per
    unique term in the book, batched into a single transaction, without
    rebuilding the whole table.
    """
    conn = _connect(db_path)
    try:
        terms = set(tokenize(body_text))
        # INSERT OR IGNORE: the (term, book_id) primary key means
        # re-running this for the same book is a no-op for terms
        # already recorded, exactly like the hierarchical structure's
        # set.add() and MongoDB's old $addToSet.
        conn.executemany(
            "INSERT OR IGNORE INTO inverted_index (term, book_id) VALUES (?, ?)",
            [(term, book_id) for term in terms],
        )
        conn.commit()
    finally:
        conn.close()


if __name__ == "__main__":
    # Command-line entry point: `python inverted_index_sqlite.py <datalake_dir> <db_path>`,
    # falling back to sensible defaults so the script also works with no
    # arguments at all.
    datalake_dir = sys.argv[1] if len(sys.argv) > 1 else "datalake"
    db_path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_DB_PATH

    n = build_index(datalake_dir, db_path)
    print(f"Indexed {n} unique terms into {db_path} (SQLite structure)")
