# C++ Data Layer Architecture & Execution Flow

This document details the pipeline architecture, codebase structure, and execution process for the C++ module in Stage 1. The goal of this phase is to build the foundational data infrastructure (Control Layer, Datalake, and Datamarts) by fetching and organizing raw texts from Project Gutenberg.

## 1. Codebase Structure

The C++ module is organized as follows:

*   **`CMakeLists.txt`:** Build configuration file enforcing C++17, compiler warnings, and linking `libcurl`.
*   **`include/ControlLayer.hpp` / `src/ControlLayer.cpp`:** Orchestrator managing pipeline state and coordinating ingestion and indexing.
*   **`include/MetadataExtractor.hpp` / `src/MetadataExtractor.cpp`:** Parses book metadata (`Title`, `Author`, `Language`) from header files and appends structured records to `datamarts/metadata.csv`.
*   **`include/InvertedIndex.hpp` / `src/InvertedIndex.cpp`:** Tokenizes cleaned body text and maintains inverted index structures in both monolithic JSON and hierarchical directory layouts.
*   **`src/main.cpp`:** Application entry point initializing `ControlLayer` and running the execution loop.

## 2. Pipeline Execution Flow

The system operates as a **Task Queue** orchestrated by `ControlLayer::step()`. During each step, the system executes only one action, prioritizing indexing over downloading.

### Phase A: State Verification (Control Layer)
1. The system reads `../control/downloaded_books.txt` and `../control/indexed_books.txt` into RAM using `std::unordered_set<int>` for O(1) lookups.
2. It calculates pending books: `downloaded - indexed`.

### Phase B: Ingestion & Datalake (If no books are pending)
If there are no pending books to index:
1. **Target Selection:** Generates a random book ID (1–70000) not yet downloaded.
2. **Download:** Uses `libcurl` to fetch the raw text from Project Gutenberg.
3. **Splitting:** Locates `*** START OF THE PROJECT GUTENBERG EBOOK` and `*** END OF THE PROJECT GUTENBERG EBOOK` to separate the header and body text.
4. **Datalake Storage:** Stores files in time-based partition format: `data/datalake/YYYYMMDD/HH/<book_id>.header.txt` and `<book_id>.body.txt`.
5. **State Update:** Appends the ID to `control/downloaded_books.txt`.

### Phase C: Datamart Construction (If books are pending)
If pending books exist, the system selects the oldest pending ID:
1. **File Retrieval:** Recursively locates `<book_id>.header.txt` and `<book_id>.body.txt` inside the datalake.
2. **Metadata Extraction:** `MetadataExtractor` parses `Title`, `Author`, and `Language` using regex and saves the record into `data/datamarts/metadata.csv`.
3. **Inverted Index Construction:** `InvertedIndex` extracts words, normalizes them to lowercase, and updates:
   - **Monolithic JSON Index:** `data/datamarts/inverted_index.json` (maps terms to sorted posting lists: `{"term": [id1, id2]}`).
   - **Hierarchical Index:** `data/datamarts/inverted_index/<Letter>/<term>.txt` (stores document IDs line-by-line).
4. **State Update:** Appends the ID to `control/indexed_books.txt`.

## 3. Benchmarking Strategy & Datamart Formats

As required by the Stage 1 guidelines and benchmarking specifications:
* **Storage Structure Comparison:** The implementation produces both single monolithic file (`inverted_index.json`) and hierarchical folder structure (`inverted_index/<A>/<term>.txt`) to benchmark file write/lookup overhead across structures and languages.
* **Release Build Performance:** Benchmarks should be compiled with release optimizations (`-O2` or `-O3`) to record execution times, indexing throughput, and memory consumption.