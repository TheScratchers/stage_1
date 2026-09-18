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
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

from pymongo import MongoClient, ASCENDING

TOKEN_PATTERN = re.compile(r"[A-Za-z]+")
DEFAULT_URI = "mongodb://localhost:27017/"


def tokenize(text: str) -> list:
    return TOKEN_PATTERN.findall(text.lower())


def get_collection(uri: str = DEFAULT_URI, db_name: str = "stage1", collection_name: str = "inverted_index"):
    client = MongoClient(uri, serverSelectionTimeoutMS=5000)
    collection = client[db_name][collection_name]
    collection.create_index([("term", ASCENDING)], unique=True)
    return client, collection


def build_index(datalake_dir: str, uri: str = DEFAULT_URI) -> int:
    """
    Scans the datalake and rebuilds the inverted index from scratch
    in MongoDB. Returns the number of unique terms stored.
    """
    postings = defaultdict(set)
    for body_file in sorted(Path(datalake_dir).rglob("*.body.txt")):
        book_id = int(body_file.name.split(".")[0])
        text = body_file.read_text(encoding="utf-8")
        for term in set(tokenize(text)):
            postings[term].add(book_id)

    client, collection = get_collection(uri)
    try:
        collection.delete_many({})  # clean rebuild
        docs = [{"term": term, "postings": sorted(ids)} for term, ids in postings.items()]
        if docs:
            collection.insert_many(docs)
        return len(docs)
    finally:
        client.close()


def search(term: str, uri: str = DEFAULT_URI) -> list:
    client, collection = get_collection(uri)
    try:
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
            collection.update_one(
                {"term": term},
                {"$addToSet": {"postings": book_id}},
                upsert=True,
            )
    finally:
        client.close()


if __name__ == "__main__":
    datalake_dir = sys.argv[1] if len(sys.argv) > 1 else "datalake"
    uri = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_URI

    n = build_index(datalake_dir, uri)
    print(f"Indexed {n} unique terms into MongoDB ({uri})")
