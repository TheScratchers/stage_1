package com.thescratchers.searchengine.benchmark;

import com.thescratchers.searchengine.datalake.Downloader;
import com.thescratchers.searchengine.datamarts.TextTokenizer;
import com.thescratchers.searchengine.datamarts.index.SqliteIndexStorage;
import com.thescratchers.searchengine.index.InvertedIndexBuilder;

import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.BenchmarkMode;
import org.openjdk.jmh.annotations.Fork;
import org.openjdk.jmh.annotations.Level;
import org.openjdk.jmh.annotations.Measurement;
import org.openjdk.jmh.annotations.Mode;
import org.openjdk.jmh.annotations.OutputTimeUnit;
import org.openjdk.jmh.annotations.Param;
import org.openjdk.jmh.annotations.Scope;
import org.openjdk.jmh.annotations.Setup;
import org.openjdk.jmh.annotations.State;
import org.openjdk.jmh.annotations.Warmup;
import org.openjdk.jmh.profile.GCProfiler;
import org.openjdk.jmh.runner.Runner;
import org.openjdk.jmh.runner.RunnerException;
import org.openjdk.jmh.runner.options.Options;
import org.openjdk.jmh.runner.options.OptionsBuilder;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.stream.Stream;

@State(Scope.Benchmark)
@BenchmarkMode(Mode.AverageTime)
@OutputTimeUnit(TimeUnit.MILLISECONDS)
public class BenchmarkRunner {

    private static final Path BOOKS_PATH    = Paths.get("../shared/books.txt");
    private static final Path WORDS_PATH    = Paths.get("../shared/words.txt");
    private static final Path DATALAKE_ROOT = Paths.get("data/datalake");

    @Param({"100", "1000", "10000"})
    public int scale;

    private Downloader           downloader;
    private SqliteIndexStorage   storage;
    private InvertedIndexBuilder indexer;

    private List<Integer>        baseBookIds;
    private List<String>         queryWords;
    private List<Integer>        syntheticIds;
    private List<List<String>>   baseTokenSets;

    @Setup(Level.Trial)
    public void setup() throws IOException {
        baseBookIds   = loadBookIds(BOOKS_PATH);
        queryWords    = loadWords(WORDS_PATH);
        downloader    = new Downloader();

        for (int bookId : baseBookIds) {
            if (findFile(bookId, ".body.txt") == null) {
                downloader.downloadBook(bookId);
            }
        }

        baseTokenSets = new ArrayList<>();
        for (int bookId : baseBookIds) {
            Path bodyFile = findFile(bookId, ".body.txt");
            if (bodyFile == null) continue;
            try {
                String content = Files.readString(bodyFile);
                List<String> tokens = TextTokenizer.tokenize(content);
                Set<String> unique = new HashSet<>(tokens);
                baseTokenSets.add(new ArrayList<>(unique));
            } catch (IOException e) {
                System.err.println("[BENCHMARK] Could not read body for book " + bookId + ": " + e.getMessage());
            }
        }

        syntheticIds = SyntheticDataGenerator.generateScale(baseBookIds, scale);
    }

    @Setup(Level.Iteration)
    public void prepareIteration() {
        File dbFile = new File("data/datamarts/inverted_index.db");
        if (dbFile.exists()) {
            dbFile.delete();
        }
        storage = new SqliteIndexStorage();
        indexer = new InvertedIndexBuilder(storage);
    }

    @Benchmark
    @Fork(value = 1)
    @Warmup(iterations = 1, time = 1)
    @Measurement(iterations = 1, time = 1)
    public void measureIndexing() {
        if (baseTokenSets.isEmpty()) return;
        int baseCount = baseTokenSets.size();
        for (int i = 0; i < syntheticIds.size(); i++) {
            int bookId = syntheticIds.get(i);
            List<String> terms = baseTokenSets.get(i % baseCount);
            storage.save(bookId, terms);
        }
    }

    @Benchmark
    @Fork(value = 1)
    @Warmup(iterations = 1, time = 1)
    @Measurement(iterations = 1, time = 1)
    public void measureQuerying() {
        for (String word : queryWords) {
            storage.search(word);
        }
    }

    private Path findFile(int bookId, String suffix) {
        if (!Files.exists(DATALAKE_ROOT)) return null;
        try (Stream<Path> stream = Files.walk(DATALAKE_ROOT)) {
            return stream
                    .filter(Files::isRegularFile)
                    .filter(p -> p.getFileName().toString().equals(bookId + suffix))
                    .findFirst()
                    .orElse(null);
        } catch (IOException e) {
            return null;
        }
    }

    private static List<Integer> loadBookIds(Path path) throws IOException {
        List<String> lines = loadLines(path);
        List<Integer> ids = new ArrayList<>(lines.size());
        for (String line : lines) {
            try {
                ids.add(Integer.parseInt(line));
            } catch (NumberFormatException e) {
                System.err.println("[BENCHMARK] Skipping non-integer in books.txt: \"" + line + "\"");
            }
        }
        return Collections.unmodifiableList(ids);
    }

    private static List<String> loadWords(Path path) throws IOException {
        List<String> lines = loadLines(path);
        if (lines.size() > 10) {
            lines = lines.subList(0, 10);
        }
        return Collections.unmodifiableList(lines);
    }

    private static List<String> loadLines(Path path) throws IOException {
        if (!Files.exists(path)) {
            throw new IOException("[BENCHMARK] Config file not found: " + path.toAbsolutePath());
        }
        List<String> result = new ArrayList<>();
        for (String line : Files.readAllLines(path)) {
            String trimmed = line.trim();
            if (trimmed.isEmpty() || trimmed.startsWith("#")) continue;
            result.add(trimmed);
        }
        return result;
    }

    public static void main(String[] args) throws RunnerException {
        Options options = new OptionsBuilder()
                .include(BenchmarkRunner.class.getSimpleName())
                .addProfiler(GCProfiler.class)
                .build();
        new Runner(options).run();
    }
}