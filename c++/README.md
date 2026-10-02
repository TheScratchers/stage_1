# Stage 1 — C++ Search Engine Data Layer

> **Author:** Pablo Martínez Suárez (TheScratchers)  
> **Course:** Big Data — Grado en Ciencia e Ingeniería de Datos, ULPGC  
> **Standard:** C++17 (Clang / GCC / MSVC)

---

## 1. Overview

The C++ module implements the complete **Data Layer** for Stage 1 of the search engine project, comprising:

1. **Datalake Storage Engine**:
   - **Time-based hierarchy:** `data/datalake/YYYYMMDD/HH/<BOOK_ID>.{header,body}.txt`
   - **Book-based hierarchy:** `data/datalake/<BOOK_ID>/<BOOK_ID>.{header,body}.txt`
   - **Batch-based hierarchy:** `data/datalake/batch_<N>/<BOOK_ID>.{header,body}.txt`
   - Full automated benchmark measuring write throughput, lookup latency, and filesystem overhead.

2. **Datamarts**:
   - **Structured Metadata (SQLite):** Stores parsed Gutenberg metadata (`book_id`, `title`, `author`, `language`, `ingested_at`, `header_path`, `body_path`) in `data/datamarts/metadata.db` with indexed lookups by author, title, and book ID.
   - **Inverted Index (3 Distinct Architectures):**
     1. **Monolithic JSON:** `data/datamarts/inverted_index.json`
     2. **Hierarchical Folders:** `data/datamarts/inverted_index_hier/<Letter>/<term>.txt`
     3. **Custom Binary Compact Index:** `data/datamarts/inverted_index.bin` with `BIDX` binary header, term dictionary table, and direct seek-based postings retrieval.

3. **Control Layer**:
   - Orchestrates downloads from Project Gutenberg via `libcurl` and coordinates indexing into all datamarts without duplication.
   - Tracks pipeline state in `control/downloaded_books.txt` and `control/indexed_books.txt`.

4. **Query & Search Engine**:
   - Single-term queries with millisecond latency comparison across all 3 index structures.
   - Boolean **AND** (postings intersection) and **OR** (postings union) search operations.
   - Metadata querying by author, title, or ID.

---

## 2. Requirements & Build Instructions

### Requirements
- C++17 compiler (`clang++` ≥ 5.0 or `g++` ≥ 7.0)
- `cmake` ≥ 3.16 (or `make`)
- `libcurl`
- `sqlite3`

### Building with CMake
```bash
cd c++
cmake -B build -S .
cmake --build build
```

### Building with Make
```bash
cd c++
make
```

The resulting binary will be placed at `c++/build/search_engine`.

---

## 3. Command-Line Interface (CLI)

The compiled executable `search_engine` supports rich subcommands:

### Run Pipeline Steps
```bash
# Run 1 pipeline step (downloads or indexes a book)
./build/search_engine step

# Run 5 pipeline steps
./build/search_engine step 5
```

### Offline Sample Dataset Test
```bash
# Ingests and indexes bundled offline sample books (11, 84, 1342)
./build/search_engine test-sample
```

### Inverted Index Queries
```bash
# Query a single term across all 3 index structures with latency comparison
./build/search_engine query truth all

# Query using a specific structure: json, hier, or bin
./build/search_engine query adventure bin

# Boolean AND query (terms intersection)
./build/search_engine query-and truth fortune

# Boolean OR query (terms union)
./build/search_engine query-or alice elizabeth
```

### Metadata Queries
```bash
# Query book by ID
./build/search_engine metadata --id 1342

# Search books by author
./build/search_engine metadata --author Austen

# Search books by title
./build/search_engine metadata --title "Pride"

# List all indexed books
./build/search_engine metadata --all
```

### Benchmarks
```bash
# Benchmark the 3 Datalake structures (saves to data/datamarts/benchmark_datalake_results.json)
./build/search_engine bench-datalake 1000

# Benchmark SQLite Metadata storage: insert speed, query by ID & Dickens/Carroll (saves to benchmark_metadata_results.json)
./build/search_engine bench-metadata 1000

# Benchmark the 3 Inverted Index structures using the 10 contract words (saves to benchmark_inverted_index_results.json)
./build/search_engine bench-index 1000

# Run all 3 benchmarks sequentially
./build/search_engine bench-all 1000
```
