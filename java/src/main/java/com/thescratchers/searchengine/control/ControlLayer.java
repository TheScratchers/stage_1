package com.thescratchers.searchengine.control;

import com.thescratchers.searchengine.datalake.Downloader;
import com.thescratchers.searchengine.datamarts.MetadataExtractor;
import com.thescratchers.searchengine.datamarts.index.SqliteIndexStorage;
import com.thescratchers.searchengine.index.InvertedIndexBuilder;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.HashSet;
import java.util.Optional;
import java.util.Random;
import java.util.Set;
import java.util.stream.Stream;

public class ControlLayer {

    private static final Path CONTROL_DIR    = Paths.get("control");
    private static final Path DOWNLOADS_FILE = CONTROL_DIR.resolve("downloaded_books.txt");
    private static final Path INDEXED_FILE   = CONTROL_DIR.resolve("indexed_books.txt");
    private static final Path DATALAKE_ROOT  = Paths.get("data/datalake");
    private static final int  TOTAL_BOOKS    = 70000;

    private final Downloader           downloader;
    private final MetadataExtractor    extractor;
    private final InvertedIndexBuilder indexer;

    public ControlLayer(Downloader downloader,
                        MetadataExtractor extractor,
                        InvertedIndexBuilder indexer) {
        this.downloader = downloader;
        this.extractor  = extractor;
        this.indexer    = indexer;
    }

    public void executePipelineStep() throws IOException {
        Files.createDirectories(CONTROL_DIR);

        Set<String> downloaded = readIds(DOWNLOADS_FILE);
        Set<String> indexed    = readIds(INDEXED_FILE);

        Set<String> pending = new HashSet<>(downloaded);
        pending.removeAll(indexed);

        if (!pending.isEmpty()) {
            String idStr = pending.iterator().next();
            int    bookId = Integer.parseInt(idStr);
            System.out.println("[CONTROL] Processing pending book " + bookId + "...");

            Optional<Path> headerFile = findFile(bookId, ".header.txt");
            Optional<Path> bodyFile   = findFile(bookId, ".body.txt");

            if (headerFile.isEmpty() || bodyFile.isEmpty()) {
                System.err.println("[CONTROL] Datalake files missing for book "
                        + bookId + ". Skipping.");
                return;
            }

            try {
                extractor.extractAndStoreMetadata(bookId, headerFile.get().toString());
                indexer.buildIndexForBook(bookId, bodyFile.get().toString());
                appendId(INDEXED_FILE, idStr);
                System.out.println("[CONTROL] Book " + bookId + " indexed and logged.");
            } catch (Exception e) {
                System.err.println("[CONTROL] Failed to process book " + bookId
                        + ": " + e.getMessage());
            }

        } else {
            Random rng = new Random();
            for (int attempt = 0; attempt < 10; attempt++) {
                int    candidateId  = rng.nextInt(TOTAL_BOOKS) + 1;
                String candidateStr = String.valueOf(candidateId);

                if (downloaded.contains(candidateStr)) continue;

                System.out.println("[CONTROL] Downloading book " + candidateId + "...");
                boolean success = downloader.downloadBook(candidateId);
                if (success) {
                    appendId(DOWNLOADS_FILE, candidateStr);
                    System.out.println("[CONTROL] Book " + candidateId
                            + " downloaded and logged.");
                } else {
                    System.err.println("[CONTROL] Download failed for book " + candidateId);
                }
                break;
            }
        }
    }

    private Optional<Path> findFile(int bookId, String suffix) throws IOException {
        if (!Files.exists(DATALAKE_ROOT)) return Optional.empty();
        try (Stream<Path> stream = Files.walk(DATALAKE_ROOT)) {
            return stream
                    .filter(Files::isRegularFile)
                    .filter(p -> p.getFileName().toString().equals(bookId + suffix))
                    .findFirst();
        }
    }

    private Set<String> readIds(Path file) throws IOException {
        if (!Files.exists(file)) return new HashSet<>();
        Set<String> ids = new HashSet<>();
        for (String line : Files.readAllLines(file)) {
            String trimmed = line.trim();
            if (!trimmed.isEmpty()) ids.add(trimmed);
        }
        return ids;
    }

    private void appendId(Path file, String id) throws IOException {
        Files.writeString(file, id + System.lineSeparator(),
                StandardOpenOption.CREATE, StandardOpenOption.APPEND);
    }

    public static void main(String[] args) {
        SqliteIndexStorage storage    = new SqliteIndexStorage();
        Downloader         downloader = new Downloader();
        MetadataExtractor  extractor  = new MetadataExtractor();
        InvertedIndexBuilder indexer  = new InvertedIndexBuilder(storage);

        ControlLayer pipeline = new ControlLayer(downloader, extractor, indexer);

        System.out.println("--- Starting Pipeline Step ---");
        try {
            pipeline.executePipelineStep();
        } catch (IOException e) {
            System.err.println("[CONTROL] Fatal IO error: " + e.getMessage());
            e.printStackTrace();
        }
        System.out.println("--- Pipeline Step Finished ---");
    }
}