# Stage 1: Java Performance Benchmark Report
**Module:** Inverted Index Storage Architectures  
**Framework:** Java Microbenchmark Harness (JMH 1.37) on JDK 24 with GC Profiler

## 1. Executive Summary & Methodology
The objective of this benchmark is to evaluate the performance and memory behavior of three distinct Java-based inverted index implementations: Relational (SQLite), Monolithic JSON, and Hierarchical JSON.

The testing environment complies strictly with the shared contract requirements:
*   **Data Ingestion:** Utilizing the 20 shared dataset books (extracted from `shared/books.txt`) looped via modulo (`i % 20`) to reach the requested benchmark scales. Books were fully tokenized into a map before indexing, using the contract tokenizer (lowercase, then maximal runs of `[A-Za-z]`, via `TextTokenizer`).
*   **Querying:** Executing batch searches over the 10 contract-specified test words from `shared/words.txt` per benchmark operation.
*   **Bulk Building:** All implementations perform massive data ingestion via a `build()` method from the pre-constructed in-memory map.
*   **Isolation:** Indexing benchmarks clear the storage layer per iteration. Query benchmarks populate the storage exclusively at the Trial level to measure read-only throughput on fully materialized indexes.

## 2. Empirical Results (Time & GC Profiling)
The benchmarks were executed measuring the Average Time (`avgt`) in milliseconds per operation (`ms/op`) across scales of 100, 1,000, and 10,000 synthetic books. Memory profiling captures `gc.alloc.rate.norm`, representing the absolute number of bytes dynamically allocated on the heap per operation (this is **allocation throughput**, not peak memory footprint). 

*Note: Query times and memory allocations reflect the cost of searching **all 10 contract words** in a single operation*.

### 2.1. Index Creation (Bulk Build Performance)
| Architecture | Scale | Time (ms/op) | Memory Allocated (Bytes/op) |
| :--- | :--- | :--- | :--- |
| **Monolithic JSON** | 100 | ~28.98 ms | ~6,997,673 B |
| **Monolithic JSON** | 1,000 | ~199.17 ms | ~74,177,998 B |
| **Monolithic JSON** | 10,000 | ~3,178.61 ms | ~2,418,311,392 B |
| **Hierarchical JSON** | 100 | ~126.02 ms | ~34,171,931 B |
| **Hierarchical JSON** | 1,000 | ~575.94 ms | ~226,102,044 B |
| **Hierarchical JSON** | 10,000 | ~2,643.63 ms | ~2,421,896,368 B |
| **SQLite (Batch)** | 100 | ~1,006.40 ms | ~161,166,668 B |
| **SQLite (Batch)** | 1,000 | ~8,196.45 ms | ~830,631,528 B |
| **SQLite (Batch)** | 10,000 | ~121,609.90 ms | ~6,260,756,048 B |

### 2.2. Term Querying (Read Performance - Batch of 10 Words)
| Architecture | Scale | Batch Query Time (ms/op) | Batch Memory Allocated (Bytes/op) |
| :--- | :--- | :--- | :--- |
| **Monolithic JSON** | 100 | ~729.73 ms | ~231,032,844 B |
| **Monolithic JSON** | 1,000 | ~3,249.70 ms | ~2,232,490,728 B |
| **Monolithic JSON** | 10,000 | ~43,198.77 ms | ~21,962,097,688 B |
| **Hierarchical JSON** | 100 | ~24.22 ms | ~12,062,381 B |
| **Hierarchical JSON** | 1,000 | ~107.94 ms | ~102,034,328 B |
| **Hierarchical JSON** | 10,000 | ~1,223.65 ms | ~989,230,792 B |
| **SQLite (B-Tree)** | 100 | ~1.04 ms | ~18,791 B |
| **SQLite (B-Tree)** | 1,000 | ~2.33 ms | ~231,251 B |
| **SQLite (B-Tree)** | 10,000 | ~13.73 ms | ~2,461,670 B |

## 3. Algorithmic Complexity & Architecture Analysis

### 3.1. Write Performance
All three architectures scale linearly ($O(N)$) during the build phase. However, SQLite carries a significant constant time penalty. At 10,000 books, SQLite takes approximately 121 seconds to build the index, compared to ~3.1 seconds for Monolithic JSON and ~2.6 seconds for Hierarchical JSON. Even with JDBC batching (`executeBatch`), the relational engine must parse SQL, manage JDBC state, and structure the B-Tree on disk. JSON bypasses this entirely via direct byte stream serialization.

### 3.2. Read Performance & Result Set Scaling
For query workloads, the roles reverse drastically. SQLite exhibits massive superiority in read performance. Searching the 10 contract words against 10,000 books takes only ~13.73 ms in SQLite. This time represents the $O(K)$ cost of JDBC mapping the specific matching rows (`ResultSet`) into a Java `List<Integer>`, proving the B-Tree index prevents full table scans.

In contrast, JSON architectures collapse under query loads at scale. Reading the 10 words from the Monolithic JSON takes over 43 seconds (~43,198 ms) at 10,000 books, as the JVM is forced to read and deserialize the entire JSON file into a `HashMap` on every single word lookup (10 full parses per operation). Hierarchical JSON improves this by isolating deserialization to specific prefix folders (~1,223 ms), but still pales in comparison to relational indexing.

### 3.3. Memory Allocation Behavior (`gc.alloc.rate.norm`)
The allocation metric (`Bytes/op`) perfectly corroborates the performance times. To query 10 words at scale 10,000, Monolithic JSON allocates a catastrophic ~21.9 GB of short-lived objects per operation (churn rate, not peak heap). Hierarchical JSON allocates ~989 MB. 
Conversely, SQLite allocates merely ~2.4 MB per operation, confirming that the relational engine yields specific data points via cursors rather than forcing the JVM to reconstruct the entire dataset in memory.