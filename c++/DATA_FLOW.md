# C++ Data Layer Architecture & Execution Flow

This document details the pipeline architecture, codebase structure, and execution process for the C++ module in Stage 1. The goal of this phase is to build the foundational data infrastructure (Control Layer, Datalake, and Datamarts) by fetching and organizing raw texts from Project Gutenberg.

## 1. Codebase Structure

The C++ module is organized as follows:

*   **`CMakeLists.txt`:** The build configuration file. It enforces the C++17 standard, configures compiler warnings, and links the required external libraries (`libcurl` for networking).
*   **`include/ControlLayer.hpp`:** The header file defining the `ControlLayer` class. It declares the properties and methods needed to track the pipeline state and orchestrate downloads and indexing tasks.
*   **`src/ControlLayer.cpp`:** The core implementation. It handles reading/writing state files, performing HTTP GET requests, text parsing, and creating the hierarchical Datalake folder structure.
*   **`src/main.cpp`:** The application entry point. It initializes the `ControlLayer` and executes the pipeline loop based on a provided step count.

## 2. Pipeline Execution Flow

The system operates as a **Task Queue** orchestrated by `ControlLayer::step()`. During each step, the system executes only one action, prioritizing indexing over downloading.

### Phase A: State Verification (Control Layer)
1. The system reads `../control/downloaded_books.txt` and `../control/indexed_books.txt` into RAM (using `std::unordered_set` for O(1) lookups).
2. It calculates the difference: *Are there downloaded books that have not been indexed yet?*

### Phase B: Ingestion & Datalake (If no books are pending)
If the state is up-to-date, the system ingests new data:
1. **Target Selection:** Generates a random book ID (1-70000) and ensures it hasn't been downloaded before.
2. **Download:** Uses `libcurl` to fetch the raw `.txt` payload from `gutenberg.org`.
3. **Splitting:** Locates the `*** START` and `*** END` markers to isolate the book. Splits the content into `header` (metadata) and `body` (novel content).
4. **Datalake Storage:** Creates a time-based directory structure (`data/datalake/YYYYMMDD/HH/`) using C++17 `<filesystem>` and `<chrono>`, saving `[id].header.txt` and `[id].body.txt`.
5. **State Update:** Appends the new ID to `downloaded_books.txt`.

### Phase C: Datamart Construction (If books are pending)
If there is a book waiting to be indexed, the system skips Phase B and proceeds to process it:
1. **Metadata Extraction (Next Step):** Will read `[id].header.txt` to parse Title/Author.
2. **Inverted Index (Next Step):** Will read `[id].body.txt` to tokenize words and map them to the book ID.
3. **State Update:** Appends the ID to `indexed_books.txt`.

## 3. Dependencies and Build Process

This module requires a C++17 compliant compiler and `libcurl`. On Windows, the recommended environment is **MinGW-w64 (MSYS2) or WinLibs**.

### Prerequisites (Windows)
Ensure you have GCC, CMake, and Ninja/Make installed and added to your system `PATH`. You also need the `curl` development headers and binaries.

### Compilation and Execution
To compile and run the project, open a terminal (e.g., Developer PowerShell or an MSYS2 terminal) and execute the following commands from the `/c++` directory:

1. **Configure the build environment:**
   ```bash
   cmake -B build -S . -G "Ninja" -DCMAKE_CXX_COMPILER=g++
   ```

2. **Build the project:**
   ```bash
   cmake --build build
   ```

3. **Run the application:**
   ```bash
   ./build/search_engine
   ```

## 4. Next Steps / To-Do

The remaining tasks for completing the C++ Data Layer implementation according to Phase C (Datamart Construction) are:

* **Metadata Extraction (Datamart):**
  * Read and parse `[id].header.txt` files to extract key metadata fields (e.g., Title, Author, Language).
  * Design the structured schema and persist metadata into a database or structured format (e.g., SQLite).

* **Inverted Index Engine (Datamart):**
  * Read and process `[id].body.txt` from the Datalake.
  * Implement text tokenization, normalization (lowercasing, punctuation stripping, stop-word removal), and mapping of terms to book IDs.
  * Implement and benchmark storage mechanisms (e.g., monolithic JSON/binary file, NoSQL/MongoDB, or custom/sharded structure).

* **Indexer Integration in Control Layer:**
  * Replace the placeholder logic in `ControlLayer::indexBook(int bookId)` with the concrete indexing and metadata extraction pipeline.
  * Ensure `indexed_books.txt` is updated upon successful completion of each book's indexing.