# Stage 1 — Building the Data Layer

> **Group:** TheScratchers  
> **Course:** Big Data — Grado en Ciencia e Ingeniería de Datos, Universidad de Las Palmas de Gran Canaria  
> **Repository:** <https://github.com/TheScratchers/stage_1>  
> **Final report:** [`stage_1_final_report.pdf`](stage_1_final_report.pdf)

A search-engine data layer for [Project Gutenberg](https://www.gutenberg.org/) books, implemented three times — **Python, Java and C++** — and benchmarked with the same dataset, tokenizer and query workload so that the storage structures (and, with caveats, the languages) can be compared.

| Member | Implementation | Folder |
|---|---|---|
| Aimar Daniel Alejandro Santana | Python | [`python/`](python/) |
| Amado Artiles Rodríguez | Java | [`java/`](java/) |
| Pablo Martínez Suárez | C++ | [`c++/`](c++/) |

---

## 1. What this repository contains

The data layer has three parts, all implemented in each language:

```
 Project Gutenberg
        │  download, cut at the START/END markers
        ▼
 ┌───────────────────┐        ┌────────────────────────────────────────────┐
 │     DATALAKE      │        │                 DATAMARTS                  │
 │  raw books, split │  ───►  │  Metadata: SQLite table books              │
 │  header / body    │        │            (book_id, title, author, lang)  │
 │                   │        │  Inverted index (3 structures):            │
 │  3 layouts:       │        │    • single monolithic JSON file           │
 │  time / book /    │        │    • hierarchical: one file per term       │
 │  batch based      │        │    • SQLite table (term, book_id)          │
 └───────────────────┘        └────────────────────────────────────────────┘
        ▲                                        ▲
        └──────────── CONTROL LAYER ─────────────┘
          control/downloaded_books.txt, control/indexed_books.txt
          one step per run: index a pending book, otherwise download a new one
```

- **Datalake.** Each book is stored as `<id>.header.txt` and `<id>.body.txt`. Three directory layouts are implemented and benchmarked: **time-based** (`yyyyMMdd/HH/`, the layout recommended by the assignment and the one used by the live pipeline), **book-based** (one directory per book) and **batch-based** (`batch_<start>-<end>/`, a fixed number of ids per directory).
- **Metadata datamart.** Title, author and language are extracted from each header with regular expressions and stored in SQLite.
- **Inverted-index datamart.** Each body is tokenized and every term is mapped to the list of book ids that contain it, in the three physical structures listed above. MongoDB was evaluated at the start but dropped by team decision (it was heavy and unreliable on one machine), and every language converged on JSON + hierarchical + SQLite.
- **Control layer.** Two plain-text files record which books are downloaded and which are indexed, so an interrupted run resumes without repeating work.

## 2. Repository structure

```
stage_1/
├── python/                  Python implementation (see python/README.md)
│   ├── src/                 downloader, datalake layouts, datamarts, control layer, benchmarks
│   ├── tests/               pytest suite
│   └── datamarts/           benchmark_*_results.json  (committed evidence)
├── java/                    Java 17 / Maven implementation (see java/REPORT.md)
│   ├── src/main/java/...    datalake, datamarts, control layer, benchmarks
│   ├── datamarts/           benchmark_datalake_results.json, benchmark_metadata_results.json
│   ├── results.json         JMH results of the inverted-index benchmark
│   └── REPORT.md            Java report and run instructions (PDF: stage_1_java_report.pdf)
├── c++/                     C++17 / CMake implementation (see c++/README.md)
│   ├── src/ include/ tests/
│   └── datamarts/           benchmark_*_results.json
├── shared/                  cross-language benchmark contract
│   ├── CONTRACT.md          dataset, tokenizer, queries, scales, rules
│   ├── books.txt            the 20 fixed Gutenberg book ids
│   └── words.txt            the 10 fixed query words
├── data/sample_books/       sample dataset: the 20 contract books, already split (offline)
├── stage_1_final_report.pdf joint report (this stage's deliverable)
(per-language reports: python/stage_1_python_report.pdf, c++/stage_1_cpp_report.pdf, java/stage_1_java_report.pdf)
```

Generated runtime data (datalakes, databases, indexes, build output) is git-ignored; only source code, the sample dataset, reports and benchmark result files are versioned.

## 3. Quick start with the sample dataset (no network needed)

`data/sample_books/` holds the 20 books of the benchmark contract, already split into header and body, so the pipeline can be tried without downloading anything.

**Python** (3.9+):

```bash
cd python
pip install -r requirements.txt
python src/metadata.py ../data/sample_books /tmp/books.db          # metadata datamart
python src/inverted_index.py ../data/sample_books /tmp/inv.json    # monolithic JSON index
python src/inverted_index_sqlite.py ../data/sample_books /tmp/inv.db
python src/inverted_index_hierarchical.py ../data/sample_books /tmp/inv
python -m pytest tests/
```

On the 20 sample books this indexes about 35,000 distinct terms in each structure. To download real books and run the live pipeline (each `control.py` step indexes one pending book or downloads a new one), see `python/README.md`.

**Java** (JDK 17+, Maven 3.8+):

```bash
cd java
mvn clean package            # builds target/benchmarks.jar and runs the unit tests
java -cp target/benchmarks.jar com.thescratchers.searchengine.control.ControlLayer   # one pipeline step (needs network)
```

**C++** (C++17 compiler, CMake ≥ 3.16, libcurl, SQLite3):

```bash
cd c++
cmake -B build -S . && cmake --build build
ctest --test-dir build --output-on-failure      # 16 test suites, 119 assertions
./build/search_engine test-sample               # offline demo on bundled sample books
./build/search_engine query truth all           # query one term across the 3 index structures
```

## 4. Reproducing the benchmarks

All three implementations follow [`shared/CONTRACT.md`](shared/CONTRACT.md): the same 20 real books (replicated by position to reach each scale), the same tokenizer (lowercase, then runs of `[A-Za-z]`), the same 10 query words, and scales of 100 / 1,000 / 10,000 books (metadata also at 100,000). The 20 real books must be downloaded once before an official run. **Benchmarks write thousands of small files and the large ones take hours** — run them on a local disk, not inside a cloud-synced folder (OneDrive, Dropbox…).

```bash
# Python (from python/)
python src/download_shared_dataset.py
python src/benchmark_datalake.py        100,1000,10000
python src/benchmark_inverted_index.py  100,1000,10000
python src/benchmark_metadata.py        100,1000,10000,100000

# Java (from java/, after mvn clean package)
java -cp target/benchmarks.jar com.thescratchers.searchengine.benchmark.DownloadContractBooks
java -cp target/benchmarks.jar com.thescratchers.searchengine.benchmark.BenchmarkRunner      # JMH index benchmark -> results.json
java -cp target/benchmarks.jar com.thescratchers.searchengine.benchmark.DatalakeBenchmark    # -> datamarts/benchmark_datalake_results.json
java -cp target/benchmarks.jar com.thescratchers.searchengine.benchmark.MetadataBenchmark    # -> datamarts/benchmark_metadata_results.json

# C++ (from c++/, after building)
./build/search_engine bench-all 100,1000,10000
```

The result files committed in each language's `datamarts/` folder (and `java/results.json`) are the raw evidence behind the report.

## 5. Results at a glance

Full tables, figures and discussion are in [`stage_1_final_report.pdf`](stage_1_final_report.pdf). Headline findings:

- **Datalake.** Write speed, lookup cost and incremental detection do not separate the three layouts; the difference is structural (time-based: 2 directories with 10,000 files in one; book-based: 10,000 directories; batch-based: directories bounded by the batch size). We keep the **time-based** layout and recommend batch-based for bursty ingestion.
- **Inverted index.** No structure wins everywhere. The monolithic JSON is fastest to query once loaded but must be loaded whole and rewritten on every update (48 s per update at 10,000 books in Python). SQLite updates in milliseconds and needs no load step, at the price of about 5× more disk and the slowest build. The hierarchical layout sits in between. SQLite is the reference structure for serving queries and incremental updates.
- **Metadata (SQLite).** Lookup by id takes between 0.1 and 2 ms in all three languages and does not grow with the table; author search is a full scan that grows with the table (tens of ms at 100,000 rows). Per-row commits dominate insertion cost: C++, which inserts in one transaction, is thousands of times faster than the per-row Python and Java pipelines.
- **Languages.** The three implementations behave the same way qualitatively (identical directory/file counts in Python and Java, correct recovery in all combinations). A ranking of languages by speed is **not** claimed: the three authors ran on different computers and some procedures differ (Table 1 of the report lists each difference, e.g. C++ lookup only checks existence and its recovery test restores 10% of the books).

## 6. Branches and workflow

| Branch | Purpose |
|---|---|
| `main` | Integrated, stable code |
| `ft/python-setup` | Python development |
| `ft/java-setup` | Java development |
| `ft/c++-setup` | C++ development |

Each member worked on a feature branch and integrated through pull requests; the Git history shows the progression of the work.

## 7. Known limitations

- Benchmarks are single runs, on different computers, without confidence intervals.
- Write throughput measures splitting and storing books that are already local, not network download.
- The C++ metadata benchmark uses its own synthetic data, one transaction and no 100,000-row scale; the C++ lookup and recovery metrics differ from Python/Java (see the report).
- Control layer is minimal on purpose (one step per run, no parallel downloaders); that belongs to later stages.
