"""
Stage 1 - Datamarts: Inverted Index (NoSQL / MongoDB structure)

Stores the inverted index in a MongoDB collection, where each document
represents one term and its postings list:

    {
        "term": "adventure",
        "postings": [5, 12, 42, 1342]
    }

Requires a MongoDB instance reachable at the given URI. For local
development, start one with Docker:

    docker run -d --name stage1-mongo -p 27017:27017 mongo:7

Trade-off: like the hierarchical structure, there's no "load the whole
index" step - every lookup is a query. Unlike a plain file, MongoDB
gives us a proper query engine and a unique index on `term` for fast
lookups, at the cost of needing a running database process instead of
just the filesystem. See benchmark_inverted_index.py for a concrete
comparison (run with --mongo, since this needs a live database).
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

from pymongo import MongoClient, ASCENDING

TOKEN_PATTERN = re.compile(r"[A-Za-z]+")
# Default connection string for a MongoDB instance running locally
# (e.g. via the docker command above), used whenever the caller
# doesn't pass a different uri.
DEFAULT_URI = "mongodb://localhost:27017/"


def tokenize(text: str) -> list:
    # .lower() first so "The" and "the" are treated as the same term;
    # findall() then returns every run of letters as a separate word.
    return TOKEN_PATTERN.findall(text.lower())


def get_collection(uri: str = DEFAULT_URI, db_name: str = "stage1", collection_name: str = "inverted_index"):
    # Opens (or reuses) a connection to the MongoDB server and returns
    # the specific collection ("table") this module stores terms in.
    client = MongoClient(uri, serverSelectionTimeoutMS=5000)
    collection = client[db_name][collection_name]
    # unique=True both speeds up search() (an index-backed lookup
    # instead of a full collection scan) and prevents two documents
    # for the same term from ever being created.
    collection.create_index([("term", ASCENDING)], unique=True)
    return client, collection


def build_index(datalake_dir: str, uri: str = DEFAULT_URI) -> int:
    """
    Scans the datalake and rebuilds the inverted index from scratch
    in MongoDB. Returns the number of unique terms stored.
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

    client, collection = get_collection(uri)
    try:
        collection.delete_many({})  # clean rebuild
        # Build one document per term up front, then send them all to
        # MongoDB in a single call (see the note on insert_many below).
        docs = [{"term": term, "postings": sorted(ids)} for term, ids in postings.items()]
        if docs:
            # insert_many batches all documents into one round trip to
            # the database, instead of one insert_one() call per term.
            collection.insert_many(docs)
        return len(docs)
    finally:
        # Always close the connection, even if insert_many() raised an
        # error above.
        client.close()


def search(term: str, uri: str = DEFAULT_URI) -> list:
    client, collection = get_collection(uri)
    try:
        # find_one() returns the single matching document (or None),
        # since `term` is unique per the index created in
        # get_collection().
        doc = collection.find_one({"term": term.lower()})
        return doc["postings"] if doc else []
    finally:
        client.close()


def update_index(book_id: int, body_text: str, uri: str = DEFAULT_URI) -> None:
    """
    Incrementally adds one book's terms using MongoDB's $addToSet,
    without rebuilding the whole collection.
    """
    client, collection = get_collection(uri)
    try:
        for term in set(tokenize(body_text)):
            # $addToSet only appends book_id if it isn't already in
            # the postings array, so re-running this for the same book
            # is safe; upsert=True creates the term's document the
            # first time it's seen.
            collection.update_one(
                {"term": term},
                {"$addToSet": {"postings": book_id}},
                upsert=True,
            )
    finally:
        client.close()


if __name__ == "__main__":
    # Command-line entry point: `python inverted_index_mongo.py <datalake_dir> <uri>`,
    # falling back to sensible defaults so the script also works with no
    # arguments at all.
    datalake_dir = sys.argv[1] if len(sys.argv) > 1 else "datalake"
    uri = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_URI

    n = build_index(datalake_dir, uri)
    print(f"Indexed {n} unique terms into MongoDB ({uri})")
