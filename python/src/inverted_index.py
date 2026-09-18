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
"""

import json
import re
import sys
from collections import defaultdict
from pathlib import Path

TOKEN_PATTERN = re.compile(r"[A-Za-z]+")


def tokenize(text: str) -> list:
    """Lowercase, alphabetic-only tokenizer."""
    return TOKEN_PATTERN.findall(text.lower())


def build_index(datalake_dir: str) -> dict:
    """
    Scans every *.body.txt file in the datalake and builds the full
    inverted index from scratch.
    """
    index = defaultdict(set)
    for body_file in sorted(Path(datalake_dir).rglob("*.body.txt")):
        book_id = int(body_file.name.split(".")[0])
        text = body_file.read_text(encoding="utf-8")
        for term in set(tokenize(text)):
            index[term].add(book_id)
    return {term: sorted(ids) for term, ids in index.items()}


def save_index(index: dict, path: str = "datamarts/inverted_index.json") -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(index, f)


def load_index(path: str = "datamarts/inverted_index.json") -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def update_index(index: dict, book_id: int, body_text: str) -> dict:
    """
    Incrementally adds one book's terms to an already-loaded index,
    without rebuilding it from scratch. Caller is responsible for
    saving the index again afterwards.
    """
    for term in set(tokenize(body_text)):
        postings = index.setdefault(term, [])
        if book_id not in postings:
            postings.append(book_id)
            postings.sort()
    return index


def search(index: dict, term: str) -> list:
    return index.get(term.lower(), [])


if __name__ == "__main__":
    datalake_dir = sys.argv[1] if len(sys.argv) > 1 else "datalake"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "datamarts/inverted_index.json"

    idx = build_index(datalake_dir)
    save_index(idx, out_path)
    print(f"Indexed {len(idx)} unique terms into {out_path}")
