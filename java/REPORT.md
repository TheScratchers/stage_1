# Stage 1: Java Performance Benchmark Report
**Module:** Inverted Index Storage Architectures  
**Framework:** Java Microbenchmark Harness (JMH 1.37) on JDK 24 with GC Profiler

## 1. Executive Summary & Methodology
The objective of this benchmark is to evaluate the performance and memory behavior of three distinct Java-based inverted index implementations: Relational (SQLite), Monolithic JSON, and Hierarchical JSON.

The testing environment strictly complies with the shared contract requirements:
*   **Data Ingestion:** Utilizing the 20 shared dataset books looped via modulo (`i % 20`) to reach the requested benchmark scales.
*   **Querying:** Executing batch searches over the 10 contract-specified test words.
*   **Bulk Building:** All implementations perform massive data ingestion via a `build()` method from a pre-constructed in-memory map.
*   **Isolation:** Indexing benchmarks clear the storage layer per iteration. Query benchmarks populate the storage exclusively at the Trial level to measure read-only throughput on fully materialized indexes.

## 2. Empirical Results (Time & GC Profiling)
The benchmarks were executed measuring the Average Time (`avgt`) in milliseconds per operation (`ms/op`) and Garbage Collection allocation rates (`gc.alloc.rate`) across scales of 100, 1,000, and 10,000 synthetic books.

### 2.1. Index Creation (Bulk Build Performance)
| Architecture | Scale | Time (ms/op) | Memory Alloc. Rate (MB/sec) |
| :--- | :--- | :--- | :--- |
| **Monolithic JSON** | 100 | ~1.33 ms | ~0.81 MB/s |
| **Monolithic JSON** | 1,000 | ~1.97 ms | ~2.17 MB/s |
| **Monolithic JSON** | 10,000 | ~6.28 ms | ~15.87 MB/s |
| **Hierarchical JSON** | 100 | ~24.88 ms | ~1.55 MB/s |
| **Hierarchical JSON** | 1,000 | ~26.63 ms | ~2.92 MB/s |
| **Hierarchical JSON** | 10,000 | ~41.43 ms | ~15.30 MB/s |
| **SQLite (Batch)** | 100 | ~1.51 ms | ~34.48 MB/s |
| **SQLite (Batch)** | 1,000 | ~13.83 ms | ~35.86 MB/s |
| **SQLite (Batch)** | 10,000 | ~161.91 ms | ~35.46 MB/s |

### 2.2. Term Querying (Read Performance)
| Architecture | Scale | Query Time (ms/op) | Query Memory Alloc. Rate (MB/sec) |
| :--- | :--- | :--- | :--- |
| **Monolithic JSON** | 100 | ~3.51 ms | ~51.43 MB/s |
| **Monolithic JSON** | 1,000 | ~7.58 ms | ~286.16 MB/s |
| **Monolithic JSON** | 10,000 | ~44.15 ms | ~436.13 MB/s |
| **Hierarchical JSON** | 100 | ~3.11 ms | ~8.91 MB/s |
| **Hierarchical JSON** | 1,000 | ~3.23 ms | ~52.77 MB/s |
| **Hierarchical JSON** | 10,000 | ~6.15 ms | ~228.44 MB/s |
| **SQLite (B-Tree)** | 100 | ~1.78 ms | ~4.67 MB/s |
| **SQLite (B-Tree)** | 1,000 | ~2.37 ms | ~12.49 MB/s |
| **SQLite (B-Tree)** | 10,000 | ~4.12 ms | ~54.96 MB/s |

## 3. Algorithmic Complexity & Architecture Analysis

### 3.1. Write Performance
All three architectures scale linearly ($O(N)$) during the build phase. However, SQLite carries a significant constant time penalty compared to the monolithic JSON implementation (taking ~161 ms vs ~6 ms at scale 10,000). Even with `PRAGMA synchronous = OFF`, an in-memory journal, and JDBC batch processing (`executeBatch`), the relational engine must parse SQL, manage JDBC state, and structure the B-Tree on disk. JSON bypasses this entirely via direct byte stream serialization. 
Interestingly, Hierarchical JSON incurs a higher cost than Monolithic JSON during ingestion (~41 ms at scale 10,000) due to the file system overhead of creating and traversing multiple prefix directories.

### 3.2. Read Performance & Result Set Scaling
The SQLite query performance degrades slightly as the scale increases (from ~1.78 ms to ~4.12 ms). This is not primarily due to B-Tree depth traversal, but rather the $O(K)$ cost of result set instantiation (where $K$ is the number of matching records). Because the benchmark loops the same 20 books, a search for a common word at scale 10,000 returns exactly 10,000 rows. The time measured represents the JDBC overhead of mapping thousands of `ResultSet` rows into a Java `List<Integer>`, rather than the index search itself.

### 3.3. Memory Allocation Behavior
The GC profiler measures Allocation Rate (MB/sec). For the JSON architectures, querying relies on `ObjectMapper` instantiating large `HashMap` structures in memory for the entire file (Monolithic) or the specific prefix folder (Hierarchical) to resolve the queries. This causes sharp spikes in short-lived object allocation during the read phase, reaching up to ~436 MB/sec for the Monolithic JSON at scale 10,000. 
SQLite circumvents mass heap allocation by yielding specific integers directly through the `ResultSet` cursor, keeping the memory footprint significantly more stable and efficient (~54.96 MB/sec at maximum scale).