# C++ Data Layer Architecture & Execution Flow

This document details the pipeline architecture, codebase structure, and execution process for the C++ module in Stage 1.

---

## 1. Codebase Structure

The C++ module is organized as follows:

*   **`CMakeLists.txt` / `Makefile`:** Build configuration enforcing C++17, `-O2` optimization, linking `libcurl` and `sqlite3`.
*   **`include/Datalake.hpp` / `src/Datalake.cpp`:** Implements 3 partitioning strategies (Time-based, Book-based, and Batch-based) and the Datalake benchmark runner.
*   **`include/MetadataExtractor.hpp` / `src/MetadataExtractor.cpp`:** Parses Gutenberg header fields (`Title`, `Author`, `Language`) and stores them in SQLite (`metadata.db`) and CSV (`metadata.csv`), with queries by author, title, and ID.
*   **`include/InvertedIndex.hpp` / `src/InvertedIndex.cpp`:** Tokenizes cleaned text and manages 3 inverted index structures (Monolithic JSON, Hierarchical Folders, and Custom Binary Compact Index `BIDX`), plus Boolean search (AND / OR) and benchmarking.
*   **`include/ControlLayer.hpp` / `src/ControlLayer.cpp`:** Orchestrator managing pipeline state (`downloaded_books.txt`, `indexed_books.txt`), automated path resolution, Gutenberg fetching, and multi-datamart updates.
*   **`src/main.cpp`:** Application entry point and CLI suite supporting pipeline execution, querying, and benchmarking.

---

## 2. Pipeline Execution Flow

The system operates as a **Task Queue** orchestrated by `ControlLayer::step()`. During each step, the system executes only one action, prioritizing indexing over downloading.

```
       ┌───────────────────────────────┐
       │   Control Layer: step()       │
       │   Reads downloaded & indexed  │
       └──────────────┬────────────────┘
                      │
           Pending books to index?
             /                 \
          YES                   NO
          /                       \
┌─────────────────────┐   ┌────────────────────────────────┐
│  Phase C: Indexing  │   │  Phase B: Ingestion (Datalake) │
│  1. Locate in lake  │   │  1. Pick unseen random ID      │
│  2. Extract metadata│   │  2. Download via libcurl       │
│  3. Save to SQLite  │   │  3. Detect START/END markers   │
│  4. Update 3 Indexes│   │  4. Split into Header & Body   │
│  5. Append indexed  │   │  5. Save to YYYYMMDD/HH/       │
└─────────────────────┘   │  6. Append downloaded          │
                          └────────────────────────────────┘
```

---

## 3. Storage Layer Details

### Datalake Partitioning
- **Time-Based:** `data/datalake/YYYYMMDD/HH/<BOOK_ID>.{header,body}.txt`
- **Book-Based:** `data/datalake/<BOOK_ID>/<BOOK_ID>.{header,body}.txt`
- **Batch-Based:** `data/datalake/batch_<N>/<BOOK_ID>.{header,body}.txt` (500 books/folder)

### Datamarts
- **SQLite Database:** `data/datamarts/metadata.db` with table `books`:
  `book_id (PK), title, author, language, header_path, body_path, ingested_at`
- **Monolithic JSON Index:** `data/datamarts/inverted_index.json`
- **Hierarchical Index:** `data/datamarts/inverted_index_hier/<Letter>/<term>.txt`
- **Binary Compact Index:** `data/datamarts/inverted_index.bin` (`BIDX` magic, term dictionary table with disk offsets, direct random seek postings lookup).