"""
Stage 1 - Datamarts: Inverted Index (Hierarchical Folder structure)

Organizes the inverted index as folders per starting letter, with one
file per term containing its postings list (one book_id per line):

    datamarts/inverted_index/
        A/
            adventure.txt
            apple.txt
        B/
            boat.txt

This trades a single large file for many small ones: updates only
touch the file(s) of the terms that changed, instead of rewriting
everything.

Trade-off: no upfront "load the whole index" cost like the monolithic
JSON structure, but every single lookup means opening one file from
disk, so the per-query cost stays roughly constant no matter how big
the overall index grows. See benchmark_inverted_index.py for a
concrete comparison.
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

TOKEN_PATTERN = re.compile(r"[A-Za-z]+")


def tokenize(text: str) -> list:
    return TOKEN_PATTERN.findall(text.lower())


def _term_path(term: str, index_root: str) -> Path:
    # Terms are alphabetic by construction (see TOKEN_PATTERN), but we
    # still fall back to an "_" bucket defensively in case this ever
    # gets called with an empty or non-alphabetic string.
    first_letter = term[0].upper() if term and term[0].isalpha() else "_"
    return Path(index_root) / first_letter / f"{term}.txt"


def build_index(datalake_dir: str, index_root: str = "datamarts/inverted_index") -> int:
    """
    Builds the hierarchical index from scratch by scanning every book
    in the datalake. Returns the number of unique terms written.
    """
    postings = defaultdict(set)
    for body_file in sorted(Path(datalake_dir).rglob("*.body.txt")):
        book_id = int(body_file.name.split(".")[0])
        text = body_file.read_text(encoding="utf-8")
        for term in set(tokenize(text)):
            postings[term].add(book_id)

    # Unlike the monolithic JSON structure (one write for the whole
    # index), a full build here means one file write per unique term -
    # this is the main cost of this structure (see the benchmark).
    for term, book_ids in postings.items():
        path = _term_path(term, index_root)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            "\n".join(str(i) for i in sorted(book_ids)), encoding="utf-8"
        )

    return len(postings)


def search(term: str, index_root: str = "datamarts/inverted_index") -> list:
    """Looks up a single term by reading only its dedicated file."""
    path = _term_path(term.lower(), index_root)
    if not path.exists():
        return []
    return [
        int(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()
    ]


def update_index(
    book_id: int, body_text: str, index_root: str = "datamarts/inverted_index"
) -> None:
    """
    Fine-grained update: adds one book to the index by only touching
    the files of the terms that appear in it, instead of rebuilding
    the whole index.
    """
    for term in set(tokenize(body_text)):
        path = _term_path(term, index_root)
        path.parent.mkdir(parents=True, exist_ok=True)
        # Read-modify-write: load the existing postings for this term
        # (if any), add this book_id, and rewrite just this one file -
        # every other term's file is untouched.
        existing = set()
        if path.exists():
            existing = {
                int(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()
            }
        existing.add(book_id)
        path.write_text(
            "\n".join(str(i) for i in sorted(existing)), encoding="utf-8"
        )


if __name__ == "__main__":
    datalake_dir = sys.argv[1] if len(sys.argv) > 1 else "datalake"
    index_root = sys.argv[2] if len(sys.argv) > 2 else "datamarts/inverted_index"

    n = build_index(datalake_dir, index_root)
    print(f"Indexed {n} unique terms into {index_root}/ (hierarchical structure)")
