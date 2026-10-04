# Stage 1 — C++ Search Engine Data Layer

> **Author:** Pablo Martínez Suárez (TheScratchers)  
> **Course:** Big Data — Grado en Ciencia e Ingeniería de Datos, ULPGC  
> **Standard:** C++17 (Clang / GCC / MSVC)  
> **Compliance:** Cross-Language Benchmark Contract (`shared/CONTRACT.md`) & Course Specification

---

## 1. Overview

The C++ module implements the complete **Data Layer** for Stage 1 of the search engine project:

1. **Datalake Storage Engine**:
   - **Time-based hierarchy:** `data/datalake/YYYYMMDD/HH/<BOOK_ID>.{header,body}.txt`
   - **Book-based hierarchy:** `data/datalake/<BOOK_ID>/<BOOK_ID>.{header,body}.txt`
   - **Batch-based hierarchy:** `data/datalake/batch_<N>/<BOOK_ID>.{header,body}.txt`
   - Automated benchmark measuring write throughput, direct lookup latency, storage overhead (directories, max depth, avg files/dir), **incremental detection cost**, and **idempotent recovery behavior**.

2. **Datamarts**:
   - **Structured Metadata (SQLite):** Stores parsed Gutenberg metadata (`book_id`, `title`, `author`, `language`, `ingested_at`, `header_path`, `body_path`) in `data/datamarts/metadata.db` with transactional batch ingestion (> 580,000 rows/s) and indexed lookups.
   - **Inverted Index (3 Distinct Architectures):**
     1. **Monolithic JSON:** `data/datamarts/inverted_index.json`
     2. **Hierarchical Sharded Folders:** `data/datamarts/inverted_index_hier/<Letter>/<term>.txt`
     3. **SQLite Relational Index:** `data/datamarts/inverted_index.db` with table `(term, book_id)` for transactional relational inverted indexing.
   - Measures build time, 10-word contract query latency across frequency tiers, update latency, disk footprint, and **RAM memory footprint via `getrusage()`**.

3. **Control Layer**:
   - Orchestrates downloads from Project Gutenberg via `libcurl` and coordinates indexing into all datamarts without duplication.
   - Tracks pipeline state in `control/downloaded_books.txt` and `control/indexed_books.txt`.

4. **Query & Search Engine**:
   - Single-term queries with millisecond latency comparison across all 3 index structures.
   - Boolean **AND** (postings intersection) and **OR** (postings union) search operations.
   - Metadata querying by author, title, or ID.

5. **Automated Unit Testing Suite**:
   - 16 test suites with 119 assertions covering Tokenizer, Metadata, Datalake layouts, Inverted Indexes, Query Engine, and Control Layer orchestration.

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

### Running Automated Tests
```bash
# Option 1: Via CTest
ctest --test-dir build --output-on-failure

# Option 2: Via Direct Test Executable
./build/run_tests

# Option 3: Via Makefile
make test
```

The build produces two binaries:
- `c++/build/search_engine`: The unified CLI search engine and benchmark runner.
- `c++/build/run_tests`: The standalone automated test suite executable.

---

## 3. Command-Line Interface (CLI)

The compiled executable `search_engine` provides rich commands:

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

# Query using a specific structure: json, hier, or sqlite
./build/search_engine query adventure sqlite

# Boolean AND query (terms intersection)
./build/search_engine query-and truth fortune

# Boolean OR query (terms union)
./build/search_engine query-or wonderland darcy
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

### Multi-Scale Benchmarks
```bash
# Benchmark Datalake layouts across multi-scale sweeps (100, 1000, 10000)
./build/search_engine bench-datalake 100,1000,10000

# Benchmark SQLite Metadata storage across scales
./build/search_engine bench-metadata 100,1000,10000,100000

# Benchmark Inverted Index structures with 10 contract words and RAM profiling
./build/search_engine bench-index 100,1000,10000

# Run all benchmarks across scales
./build/search_engine bench-all 100,1000,10000
```

### Benchmark Datamarts & PDF Report
All empirical benchmark metrics are version-controlled in `c++/datamarts/`:
- `c++/datamarts/benchmark_datalake_results.json`
- `c++/datamarts/benchmark_metadata_results.json`
- `c++/datamarts/benchmark_inverted_index_results.json`

To regenerate the technical report PDF (`c++/stage_1_cpp_report.pdf` and `stage_1_cpp_report.pdf`):
```bash
python3 c++/generate_report.py
```
