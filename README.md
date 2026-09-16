# Stage 1 — Building the Data Layer

> **Team:** TheScratchers  
> **Course:** Big Data — Grado en Ciencia e Ingeniería de Datos, Universidad de Las Palmas de Gran Canaria  
> **Repository:** [github.com/TheScratchers/stage_1](https://github.com/TheScratchers/stage_1)

---

## Team Members

| Name | Role |
|---|---|
| **Amado Artiles Rodríguez** | Java implementation |
| **Aimar Daniel Alejandro Santana** | Python implementation |
| **Pablo Martínez Suárez** | C++ implementation |

---

## Project Overview

This repository contains **Stage 1** of a search engine built from scratch as part of the Big Data course project. The goal of this stage is to design and implement the **data layer**, which serves as the foundation for all subsequent stages of the pipeline.

The data layer consists of two complementary storage systems:

- **Datalake** — stores raw and cleaned book content fetched from [Project Gutenberg](https://www.gutenberg.org/), organized in a time-based directory hierarchy.
- **Datamarts** — stores structured, queryable data: book metadata and inverted indexes that enable fast search operations.

A minimal **control layer** coordinates the pipeline, tracking which books have been downloaded and indexed.

---

## Stage 1 Scope

### 1. Data Source — Project Gutenberg

Books are fetched directly from Project Gutenberg using their numeric IDs. Each book is split into:
- `<BOOK_ID>.header.txt` — metadata (title, author, language, release date…)
- `<BOOK_ID>.body.txt` — cleaned body text

**Example:** Book ID `1342` (Pride and Prejudice) →  
`https://www.gutenberg.org/cache/epub/1342/pg1342.txt`

### 2. Datalake — Time-Based Hierarchy

Downloaded books are stored in a time-based directory layout that mirrors practices in distributed storage systems such as HDFS and Amazon S3:

```
data/datalake/
└── YYYYMMDD/
    └── HH/
        ├── <BOOK_ID>.body.txt
        └── <BOOK_ID>.header.txt
```

This structure enables:
- **Traceability** — know exactly when each book was ingested.
- **Incremental processing** — index only the newest partitions.
- **Scalability** — avoid single-directory bottlenecks as data grows.

The benchmark also evaluates alternative structures (book-based hierarchy, batch/range-based hierarchy) to justify the final design choice.

### 3. Datamarts

#### 3.1 Metadata
Book metadata is extracted from each header file using regex and stored in a structured database (e.g., SQLite). The schema includes:

```
book_id | title | author | language
```

#### 3.2 Inverted Index
The inverted index maps each term to the list of book IDs where it appears:

```json
{
  "adventure": [5, 12, 42],
  "island":    [5, 1342],
  "shipwreck": [12, 17]
}
```

Three storage approaches are benchmarked:
- **Single monolithic file** (JSON / binary)
- **NoSQL database** (MongoDB)
- **Custom / sharded approach**

### 4. Control Layer

A minimal control layer coordinates downloads and indexing using two plain-text state files:

```
control/
├── downloaded_books.txt   ← IDs of successfully downloaded books
└── indexed_books.txt      ← IDs of successfully indexed books
```

The orchestration logic:
1. Check for downloaded-but-not-yet-indexed books → schedule them for indexing.
2. If none pending → download a new book from Project Gutenberg (avoiding duplicates).
3. Update state files after every operation.

### 5. Benchmarking

All three implementations (C++, Java, Python) process the **same dataset** with identical preprocessing rules to ensure fair comparison across:

| Metric | Description |
|---|---|
| **Indexing speed** | Time to build the inverted index |
| **Query performance** | Average response time per query |
| **Update performance** | Cost of adding new books without full rebuild |
| **Memory & disk usage** | RAM and storage required |
| **Scalability** | How performance changes as corpus grows |
| **Download/write throughput** | Books downloaded and stored per unit time |

---

## Repository Structure

```
stage_1/
├── c++/                    ← C++ implementation (Pablo Martínez Suárez)
│   ├── src/                ← Source files (.cpp)
│   ├── include/            ← Header files (.h / .hpp)
│   ├── build/              ← CMake build output (git-ignored)
│   └── CMakeLists.txt      ← C++17 CMake configuration
├── java/                   ← Java implementation (Amado Artiles Rodríguez)
├── python/                 ← Python implementation (Aimar Daniel Alejandro Santana)
├── data/                   ← Datalake & datamarts runtime data (git-ignored)
├── control/
│   ├── downloaded_books.txt
│   └── indexed_books.txt
├── .gitignore
└── README.md
```

---

## Setup & Execution

### C++ (CMake / C++17)

```bash
cd c++
cmake -B build -S .
cmake --build build
./build/search_engine
```

**Requirements:** CMake ≥ 3.16, a C++17-compatible compiler (GCC ≥ 7, Clang ≥ 5, MSVC 2017+).

### Java

```bash
cd java
# Instructions will be added during ft/java-setup
```

### Python

```bash
cd python
# Instructions will be added during ft/python-setup
```

---

## Branches

| Branch | Purpose |
|---|---|
| `main` | Stable, integrated code |
| `ft/c++-setup` | C++ module development |
| `ft/java-setup` | Java module development |
| `ft/python-setup` | Python module development |

---

## Deliverables

The final deliverable for Stage 1 is a **PDF report** submitted to the virtual campus, containing:
1. Cover page (course, title, team members, group name, repository URL)
2. Introduction and objectives
3. System architecture
4. Design decisions
5. Benchmarks and results (≥ 3 languages, datalake structures, inverted-index structures)
6. Conclusions and future improvements
