"""
Stage 1 - Thin helper for reading the cross-language benchmark
contract (../shared/CONTRACT.md, books.txt, words.txt) from the
Python benchmark scripts, so the parsing logic for those shared files
lives in exactly one place instead of being copy-pasted into every
script that needs it.

Assumes the same convention as every other script here: run from the
python/ directory (e.g. `python src/benchmark_datalake.py`), so
../shared/ resolves to the repo-root shared/ folder.
"""

from pathlib import Path

SHARED_DIR = Path("../shared")
BOOKS_FILE = SHARED_DIR / "books.txt"
WORDS_FILE = SHARED_DIR / "words.txt"

# Fixed by shared/CONTRACT.md Section 5 (Metadata stress test). There's
# no authors.txt - these 2 names are agreed directly in the contract
# text, picked because they appear in a different number of the 20
# contract books (see books.txt): Dickens in 3 (98, 46, 1400), Carroll
# in just 1 (11).
METADATA_REPEATED_AUTHOR = "Charles Dickens"
METADATA_UNIQUE_AUTHOR = "Lewis Carroll"


def _read_lines(path: Path) -> list:
    """Shared parser for both books.txt and words.txt: one entry per
    line, ignoring blank lines and "#" comments."""
    lines = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        lines.append(line)
    return lines


def load_book_ids() -> list:
    """Returns the 20 contract book ids as ints, in file order."""
    return [int(x) for x in _read_lines(BOOKS_FILE)]


def load_words() -> list:
    """Returns the 10 contract query words, in file order."""
    return _read_lines(WORDS_FILE)
