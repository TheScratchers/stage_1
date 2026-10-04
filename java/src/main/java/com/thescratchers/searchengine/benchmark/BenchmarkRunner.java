package com.thescratchers.searchengine.benchmark;

import com.thescratchers.searchengine.datamarts.index.*;
import org.openjdk.jmh.annotations.*;
import org.openjdk.jmh.profile.GCProfiler;
import org.openjdk.jmh.runner.Runner;
import org.openjdk.jmh.runner.options.Options;
import org.openjdk.jmh.runner.options.OptionsBuilder;

import java.io.File;
import java.util.*;
import java.util.concurrent.TimeUnit;

@State(Scope.Benchmark)
@OutputTimeUnit(TimeUnit.MILLISECONDS)
@BenchmarkMode(Mode.AverageTime)
@Fork(1)
@Warmup(iterations = 1, time = 1)
@Measurement(iterations = 1, time = 1)
public class BenchmarkRunner {

    @Param({"100", "1000", "10000"})
    private int scale;

    private Map<String, List<Integer>> memoryIndex;
    private SqliteIndexStorage sqliteStorage;
    private JsonMonolithicIndexStorage monolithicStorage;
    private JsonHierarchicalIndexStorage hierarchicalStorage;

    private void cleanDirectory(File directory) {
        if (directory.exists()) {
            for (File file : Objects.requireNonNull(directory.listFiles())) {
                if (file.isDirectory()) cleanDirectory(file);
                file.delete();
            }
        }
    }

    @Setup(Level.Iteration)
    public void prepareIndexing() {
        cleanDirectory(new File("data/datamarts"));
        new File("data/datamarts").mkdirs();

        memoryIndex = new HashMap<>();
        for (int i = 1; i <= scale; i++) {
            for (int w = 0; w < 500; w++) {
                memoryIndex.computeIfAbsent("term" + w, k -> new ArrayList<>()).add(i);
            }
        }

        sqliteStorage = new SqliteIndexStorage();
        monolithicStorage = new JsonMonolithicIndexStorage();
        hierarchicalStorage = new JsonHierarchicalIndexStorage();
    }

    @State(Scope.Thread)
    public static class QueryState {
        SqliteIndexStorage querySqlite;
        JsonMonolithicIndexStorage queryMonolithic;
        JsonHierarchicalIndexStorage queryHierarchical;

        @Setup(Level.Trial)
        public void setUp(BenchmarkRunner runner) {
            runner.prepareIndexing();
            
            querySqlite = runner.sqliteStorage;
            querySqlite.build(runner.memoryIndex);
            
            queryMonolithic = runner.monolithicStorage;
            queryMonolithic.build(runner.memoryIndex);
            
            queryHierarchical = runner.hierarchicalStorage;
            queryHierarchical.build(runner.memoryIndex);
        }
    }

    @Benchmark
    public void measureIndexingSqlite() { sqliteStorage.build(memoryIndex); }

    @Benchmark
    public void measureIndexingJsonMonolithic() { monolithicStorage.build(memoryIndex); }

    @Benchmark
    public void measureIndexingJsonHierarchical() { hierarchicalStorage.build(memoryIndex); }

    @Benchmark
    public void measureQueryingSqlite(QueryState state) { state.querySqlite.search("term100"); }

    @Benchmark
    public void measureQueryingJsonMonolithic(QueryState state) { state.queryMonolithic.search("term100"); }

    @Benchmark
    public void measureQueryingJsonHierarchical(QueryState state) { state.queryHierarchical.search("term100"); }

    public static void main(String[] args) throws Exception {
        Options opt = new OptionsBuilder()
                .include(BenchmarkRunner.class.getSimpleName())
                .addProfiler(GCProfiler.class)
                .resultFormat(org.openjdk.jmh.results.format.ResultFormatType.JSON)
                .result("results.json")
                .build();
        new Runner(opt).run();
    }
}