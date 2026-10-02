# Stage 1 - Cross-Language Benchmark Contract

## Why this file exists

Section 4.2 of the project spec requires: "The benchmark must also
compare implementations in at least three programming languages. The
same dataset, tokenizer, normalization rules, and query workload must
be used in all cases so that results are comparable across languages."

This document (plus `books.txt` and `words.txt` in this same folder)
is the single source of truth every language implementation (Python,
Java, C++) must follow so that our three benchmarks are actually
comparable, instead of each language benchmarking its own thing.

Any change to this contract should be agreed by all three team
members before being merged into `main` - it affects everyone's
benchmark code, not just one person's branch.

## 1. Shared dataset (`books.txt`)

20 real Project Gutenberg books, listed in `books.txt` (one book_id
per line, with title/author as comments for reference). The first 3
(11, 84, 1342) are the same 3 books already bundled as the offline
sample dataset in `data/sample_books/` (see `c++/README.md`,
`test-sample`), so nothing changes for anyone already using those.
The other 17 are new additions for this contract.

Every language's datalake/indexing benchmark must ingest exactly
these 20 real books (not a different, larger, or smaller set) before
doing any synthetic volume scaling on top of them.

## 2. Tokenizer / normalization rule

To guarantee the resulting vocabulary (and therefore the inverted
index built from it) is identical across languages regardless of
which language parsed the text, every indexer must tokenize text
using exactly this rule:

1. Lowercase the entire text first.
2. Extract maximal runs of ASCII letters (regex `[A-Za-z]+` or the
   equivalent in that language) as tokens - numbers and punctuation
   are dropped entirely.
3. No stemming, no lemmatization, no stopword removal.

This is already what Python's `inverted_index.py` does:
`TOKEN_PATTERN = re.compile(r"[A-Za-z]+")` applied to `text.lower()`.
Java and C++ must replicate the exact same rule (not an
equivalent-but-different one), so the exact same set of terms comes
out of the exact same book regardless of language.

## 3. Query workload (`words.txt`)

10 fixed search terms, listed in `words.txt`, deliberately split
across 3 frequency tiers (common / medium / rare - see the comments
in that file) so the benchmark actually stresses each index structure
differently instead of all 10 words behaving the same way.

Every language's inverted-index benchmark (query performance, update
performance) must query exactly these 10 words - not a random or
per-language sample. These tiers are a best-effort assignment based
on the books' known content, not yet a measured frequency count - see
the note in `words.txt` about sanity-checking them once all 20 books
are actually indexed.

## 4. Synthetic volume scales

For the scalability dimension (datalake, metadata, and inverted-index
benchmarks), synthetic volumes are built by replicating the 20 real
books' content across synthetic ids, at these 3 scales:

- 100 books
- 1,000 books
- 10,000 books

(Updated from an earlier draft of 1,000/10,000/100,000: these smaller,
still log-spaced scales - a 10x step each time, 100x range overall -
already show a clear scalability trend while keeping each language's
benchmark runtime reasonable. These are also the scales Pablo's C++
implementation already uses, so adopting them here needs no rework on
his side.)

Every language should report its throughput/latency numbers at all 3
scales, so the trend across scales is comparable language-to-language.

## 5. Metadata stress test (team extension, not required by the spec)

Section 4.1 of the project spec does NOT require comparing metadata
storage across the three programming languages (the only comparison
it mentions - SQLite vs. PostgreSQL/MySQL vs. MongoDB/Redis - is
explicitly "optional but recommended", and it's a comparison between
database engines, not between languages). The team has nonetheless
decided to all benchmark our own metadata storage choice (whatever
each language uses - SQLite, etc.) at increasing scale, to find its
stress limit/bottleneck. Since all three of us are doing this, it
needs the same standardization as the required benchmarks above, or
the numbers won't be comparable between languages.

Using the same 20-book dataset and the same 3 synthetic scales
defined above, every language's metadata benchmark must measure:

- **Insertion speed**: time to insert N synthetic rows.
- **Query performance - `find_by_id`**: average time for a
  primary-key lookup, sampled over a reasonable number of random ids
  (the specific ids don't need to match across languages - a
  primary-key lookup's cost depends on table size, not which id).
- **Query performance - `find_by_author`**: average time for an
  author lookup (`LIKE`/substring match or equivalent), using exactly
  these 2 fixed authors so every language scans a comparable number
  of matching rows:
  - **Repeated author**: `Charles Dickens` (appears in 3 of the 20
    books: 98, 46, 1400) - exercises the query against a larger
    matching set.
  - **Unique author**: `Lewis Carroll` (appears in only 1 of the 20
    books: 11) - exercises the query against a minimal matching set.
- **Scalability**: the trend across the 3 scales for both queries
  above, same as the other two benchmarks.

## 6. What each language must report

For a result to be comparable, report at minimum:

- **Datalake**: write throughput, lookup cost, incremental-processing
  cost, recovery behavior, storage overhead (per structure, per
  scale).
- **Inverted index**: indexing speed, query performance (using the 10
  words above), update performance, memory/disk usage, scalability
  (per structure, per scale).
- **Metadata** (team extension - see Section 5): insertion speed,
  `find_by_id` and `find_by_author` query performance (using the 2
  fixed authors above), scalability (per scale).

## 7. Status

- [x] Dataset and query workload drafted
- [ ] Reviewed and approved by Amado (Java)
- [ ] Reviewed and approved by Pablo (C++)
- [ ] Python benchmarks updated to use this contract
- [ ] Java benchmarks updated to use this contract
- [ ] C++ benchmarks updated to use this contract
