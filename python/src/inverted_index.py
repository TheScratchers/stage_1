"""
Stage 1 - Datamarts: Inverted Index (Single Monolithic File structure)

Builds an inverted index from the book bodies in the datalake and stores
it as a single JSON file mapping each term to the sorted list of
book_ids where it appears:

    {
        "adventure": [5, 12, 42],
        "island": [5, 1342]
    }

Saved by default at: datamarts/inverted_index.json

Trade-off: cheap once loaded (an app loads this file into memory once
at startup, then every lookup is a plain dict access), but the whole
file has to be parsed before the very first query can be answered, and
that parsing cost grows with the size of the index. See
benchmark_inverted_index.py for a concrete comparison against the
hierarchical and SQLite structures.
"""

import json
import re
import sys
from collections import defaultdict
from pathlib import Path

# Alphabetic-only: numbers and punctuation are dropped from the index
# entirely, so a search only ever needs to match words.
TOKEN_PATTERN = re.compile(r"[A-Za-z]+")


def tokenize(text: str) -> list:
    """Lowercase, alphabetic-only tokenizer."""
    # .lower() first so "The" and "the" are treated as the same term;
    # findall() then returns every run of letters as a separate word.
    return TOKEN_PATTERN.findall(text.lower())


def build_index(datalake_dir: str) -> dict:
    """
    Scans every *.body.txt file in the datalake and builds the full
    inverted index from scratch.
    """
    # defaultdict(set): looking up a term that hasn't been seen yet
    # automatically creates an empty set for it, so we can just call
    # .add() without checking "does this term already have an entry?".
    index = defaultdict(set)
    # sorted() already returns a list, so wrapping it in a variable
    # here doesn't change what's iterated or in what order - it just
    # lets us also report "X/N books processed" below. Purely
    # additive: nothing about how `index` is built changes.
    body_files = sorted(Path(datalake_dir).rglob("*.body.txt"))
    for i, body_file in enumerate(body_files):
        # The book_id is the part of the filename before the first
        # "." (e.g. "1342.body.txt" -> "1342" -> 1342).
        book_id = int(body_file.name.split(".")[0])
        text = body_file.read_text(encoding="utf-8")
        # set(...) de-duplicates repeated words within the same book,
        # since we only need to know THAT a term appears in book_id,
        # not how many times.
        for term in set(tokenize(text)):
            index[term].add(book_id)
        # Heartbeat for large scales: this loop alone can run for
        # minutes with no other output, which looks indistinguishable
        # from a hang. Every 200 books is frequent enough to reassure
        # without flooding the console.
        if (i + 1) % 200 == 0:
            print(f"    ...processed {i + 1}/{len(body_files)} books")
    # Convert the internal {term: set(book_ids)} into the final
    # {term: [sorted book_ids]} shape used for storage/output.
    return {term: sorted(ids) for term, ids in index.items()}


def save_index(index: dict, path: str = "datamarts/inverted_index.json") -> None:
    # Make sure the parent folder (e.g. datamarts/) exists, then write
    # the whole index as one JSON file in a single call.
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(index, f)


def load_index(path: str = "datamarts/inverted_index.json") -> dict:
    # Reads and parses the whole file back into a dict in memory - see
    # the module docstring for why this "cold load" cost matters.
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def update_index(index: dict, book_id: int, body_text: str) -> dict:
    """
    Incrementally adds one book's terms to an already-loaded index,
    without rebuilding it from scratch. Caller is responsible for
    saving the index again afterwards.
    """
    for term in set(tokenize(body_text)):
        # setdefault(term, []) returns the existing postings list for
        # this term, or creates and returns a new empty one if it's a
        # brand new term.
        postings = index.setdefault(term, [])
        # Guard against re-indexing the same book twice (e.g. control.py
        # re-running a step): without this check, running it again
        # would append a duplicate book_id to the postings list.
        if book_id not in postings:
            postings.append(book_id)
            # Keep the list sorted so search() always returns postings
            # in a consistent, predictable order.
            postings.sort()
    return index


def search(index: dict, term: str) -> list:
    # .get(..., []) returns an empty list instead of raising an error
    # when the term was never indexed.
    return index.get(term.lower(), [])


if __name__ == "__main__":
    # Command-line entry point: `python inverted_index.py <datalake_dir> <out_path>`,
    # falling back to sensible defaults so the script also works with no
    # arguments at all.
    datalake_dir = sys.argv[1] if len(sys.argv) > 1 else "datalake"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "datamarts/inverted_index.json"

    idx = build_index(datalake_dir)
    save_index(idx, out_path)
    print(f"Indexed {len(idx)} unique terms into {out_path}")
