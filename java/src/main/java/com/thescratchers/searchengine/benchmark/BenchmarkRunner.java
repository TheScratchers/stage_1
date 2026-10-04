package com.thescratchers.searchengine.benchmark;

import com.thescratchers.searchengine.datamarts.index.*;
import org.openjdk.jmh.annotations.*;
import org.openjdk.jmh.infra.Blackhole;
import org.openjdk.jmh.profile.GCProfiler;
import org.openjdk.jmh.results.format.ResultFormatType;
import org.openjdk.jmh.runner.Runner;
import org.openjdk.jmh.runner.options.Options;
import org.openjdk.jmh.runner.options.OptionsBuilder;

import java.io.File;
import java.util.*;
import java.util.concurrent.TimeUnit;

@BenchmarkMode(Mode.AverageTime)
@OutputTimeUnit(TimeUnit.MILLISECONDS)
@Fork(1)
@Warmup(iterations = 1, time = 1)
@Measurement(iterations = 1, time = 1)
public class BenchmarkRunner {

    @State(Scope.Thread)
    public static class IndexingState {
        @Param({"100", "1000", "10000"})
        public int scale;

        public Map<String, List<Integer>> memoryIndex;
        public SqliteIndexStorage sqliteStorage;
        public JsonMonolithicIndexStorage monolithicStorage;
        public JsonHierarchicalIndexStorage hierarchicalStorage;

        @Setup(Level.Iteration)
        public void setUp() {
            cleanDirectory(new File("data/datamarts"));
            new File("data/datamarts").mkdirs();

            memoryIndex = generateMemoryIndex(scale);

            sqliteStorage = new SqliteIndexStorage();
            monolithicStorage = new JsonMonolithicIndexStorage();
            hierarchicalStorage = new JsonHierarchicalIndexStorage();
        }
    }

    @State(Scope.Thread)
    public static class QueryingState {
        @Param({"100", "1000", "10000"})
        public int scale;

        public SqliteIndexStorage sqliteStorage;
        public JsonMonolithicIndexStorage monolithicStorage;
        public JsonHierarchicalIndexStorage hierarchicalStorage;
        public List<String> queryWords;

        @Setup(Level.Trial)
        public void setUp() {
            cleanDirectory(new File("data/datamarts"));
            new File("data/datamarts").mkdirs();

            Map<String, List<Integer>> memoryIndex = generateMemoryIndex(scale);

            sqliteStorage = new SqliteIndexStorage();
            monolithicStorage = new JsonMonolithicIndexStorage();
            hierarchicalStorage = new JsonHierarchicalIndexStorage();

            sqliteStorage.build(memoryIndex);
            monolithicStorage.build(memoryIndex);
            hierarchicalStorage.build(memoryIndex);

            queryWords = Arrays.asList(
                    "project", "gutenberg", "alice", "wonderland", "adventure",
                    "science", "history", "computer", "system", "data"
            );
        }
    }

    private static void cleanDirectory(File directory) {
        if (directory.exists()) {
            File[] files = directory.listFiles();
            if (files != null) {
                for (File file : files) {
                    if (file.isDirectory()) {
                        cleanDirectory(file);
                    }
                    file.delete();
                }
            }
        }
    }

    private static Map<String, List<Integer>> generateMemoryIndex(int scale) {
        String[] books = new String[]{
                "the project gutenberg ebook of alice in wonderland adventure",
                "computer science history system data analysis machine learning",
                "artificial intelligence robotics cybernetics automation network",
                "software engineering design patterns architecture algorithms",
                "operating systems memory management virtualization security",
                "database relational nosql distributed cluster replication",
                "web development frontend backend fullstack javascript html css",
                "cloud computing serverless microservices container docker",
                "machine learning deep neural networks tensorflow pytorch",
                "natural language processing text mining sentiment analysis",
                "computer vision image recognition object detection tracking",
                "data science big analytics hadoop spark flink streaming",
                "cryptography encryption decryption hashing digital signatures",
                "blockchain cryptocurrency bitcoin ethereum smart contracts",
                "internet of things iot sensors edge computing fog",
                "cybersecurity ethical hacking penetration testing malware",
                "bioinformatics genomics sequencing dna protein structure",
                "quantum computing qubits superposition entanglement gates",
                "human computer interaction ui ux usability accessibility",
                "computer graphics rendering ray tracing virtual reality"
        };

        Map<String, List<Integer>> memoryIndex = new HashMap<>();
        for (int i = 1; i <= scale; i++) {
            String content = books[i % 20];
            String[] tokens = content.split("\\s+");
            Set<String> uniqueTokens = new HashSet<>(Arrays.asList(tokens));
            for (String token : uniqueTokens) {
                memoryIndex.computeIfAbsent(token, k -> new ArrayList<>()).add(i);
            }
        }
        return memoryIndex;
    }

    @Benchmark
    public void measureIndexingSqlite(IndexingState state) {
        state.sqliteStorage.build(state.memoryIndex);
    }

    @Benchmark
    public void measureIndexingJsonMonolithic(IndexingState state) {
        state.monolithicStorage.build(state.memoryIndex);
    }

    @Benchmark
    public void measureIndexingJsonHierarchical(IndexingState state) {
        state.hierarchicalStorage.build(state.memoryIndex);
    }

    @Benchmark
    public void measureQueryingSqlite(QueryingState state, Blackhole blackhole) {
        for (String word : state.queryWords) {
            blackhole.consume(state.sqliteStorage.search(word));
        }
    }

    @Benchmark
    public void measureQueryingJsonMonolithic(QueryingState state, Blackhole blackhole) {
        for (String word : state.queryWords) {
            blackhole.consume(state.monolithicStorage.search(word));
        }
    }

    @Benchmark
    public void measureQueryingJsonHierarchical(QueryingState state, Blackhole blackhole) {
        for (String word : state.queryWords) {
            blackhole.consume(state.hierarchicalStorage.search(word));
        }
    }

    public static void main(String[] args) throws Exception {
        Options opt = new OptionsBuilder()
                .include(BenchmarkRunner.class.getSimpleName())
                .addProfiler(GCProfiler.class)
                .resultFormat(ResultFormatType.JSON)
                .result("results.json")
                .build();
        new Runner(opt).run();
    }
}