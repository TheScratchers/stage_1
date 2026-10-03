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
    # .lower() first so "The" and "the" are treated as the same term;
    # findall() then returns every run of letters as a separate word.
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
    # First pass: build the whole index in memory (one set of book_ids
    # per term), the same way build_index() does in inverted_index.py.
    postings = defaultdict(set)
    # sorted() already returns a list, so wrapping it in a variable
    # here doesn't change what's iterated or in what order - it just
    # lets us also report "X/N books processed" below. Purely
    # additive: nothing about how `postings` is built changes.
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

    # Second pass: unlike the monolithic JSON structure (one write for
    # the whole index), a full build here means one file write per
    # unique term - this is the main cost of this structure (see the
    # benchmark).
    for term, book_ids in postings.items():
        path = _term_path(term, index_root)
        # Create the letter subfolder (e.g. A/) the first time it's
        # needed; exist_ok=True means later terms in the same letter
        # don't error out.
        path.parent.mkdir(parents=True, exist_ok=True)
        # One book_id per line, sorted for a predictable file layout.
        path.write_text(
            "\n".join(str(i) for i in sorted(book_ids)), encoding="utf-8"
        )

    return len(postings)


def search(term: str, index_root: str = "datamarts/inverted_index") -> list:
    """Looks up a single term by reading only its dedicated file."""
    path = _term_path(term.lower(), index_root)
    if not path.exists():
        # Term was never indexed - same "not found" result as the
        # other two structures' search() functions.
        return []
    # Read the file, split it into lines, and convert each non-empty
    # line back into an integer book_id.
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
        # set.add() is a no-op if book_id is already present, so this
        # update is safe to run twice for the same book.
        existing.add(book_id)
        path.write_text(
            "\n".join(str(i) for i in sorted(existing)), encoding="utf-8"
        )


if __name__ == "__main__":
    # Command-line entry point: `python inverted_index_hierarchical.py <datalake_dir> <index_root>`,
    # falling back to sensible defaults so the script also works with no
    # arguments at all.
    datalake_dir = sys.argv[1] if len(sys.argv) > 1 else "datalake"
    index_root = sys.argv[2] if len(sys.argv) > 2 else "datamarts/inverted_index"

    n = build_index(datalake_dir, index_root)
    print(f"Indexed {n} unique terms into {index_root}/ (hierarchical structure)")
