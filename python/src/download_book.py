"""
Stage 1 - Data Source
Downloads a book from Project Gutenberg, detects the header/body/footer
markers, and saves the header and body as separate text files.

Usage (standalone test):
    python download_book.py 1342 ../../datalake/raw_test
"""

import sys
from pathlib import Path

import requests

START_MARKER = "*** START OF THE PROJECT GUTENBERG EBOOK"
END_MARKER = "*** END OF THE PROJECT GUTENBERG EBOOK"


def download_book(book_id: int, output_path: str) -> bool:
    """
    Downloads a single book from Project Gutenberg and splits it into
    header and body files.

    Returns True on success, False if the book could not be parsed
    (e.g. markers not found) or downloaded.
    """
    output_dir = Path(output_path)
    output_dir.mkdir(parents=True, exist_ok=True)

    url = f"https://www.gutenberg.org/cache/epub/{book_id}/pg{book_id}.txt"

    try:
        response = requests.get(url, timeout=30)
        response.raise_for_status()
    except requests.RequestException as exc:
        print(f"[download_book] Failed to download book {book_id}: {exc}")
        return False

    text = response.text

    if START_MARKER not in text or END_MARKER not in text:
        print(f"[download_book] Markers not found for book {book_id}")
        return False

    header, body_and_footer = text.split(START_MARKER, 1)
    body, footer = body_and_footer.split(END_MARKER, 1)

    body_path = output_dir / f"{book_id}.body.txt"
    header_path = output_dir / f"{book_id}.header.txt"

    with open(body_path, "w", encoding="utf-8") as f:
        f.write(body.strip())

    with open(header_path, "w", encoding="utf-8") as f:
        f.write(header.strip())

    return True


if __name__ == "__main__":
    book_id = int(sys.argv[1]) if len(sys.argv) > 1 else 1342
    out_path = sys.argv[2] if len(sys.argv) > 2 else "data/output"

    success = download_book(book_id, out_path)
    if success:
        print(f"Book {book_id} downloaded and split successfully into {out_path}")
    else:
        print(f"Failed to process book {book_id}")
        sys.exit(1)
