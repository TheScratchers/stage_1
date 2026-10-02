package com.thescratchers.searchengine.control;

import com.thescratchers.searchengine.datalake.Downloader;
import com.thescratchers.searchengine.datamarts.MetadataExtractor;
import com.thescratchers.searchengine.datamarts.index.InvertedIndexStorage;
import com.thescratchers.searchengine.datamarts.index.JsonIndexStorage;
import com.thescratchers.searchengine.index.InvertedIndexBuilder;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.HashSet;
import java.util.Random;
import java.util.Set;

/**
 * Capa de control del pipeline de datos.
 *
 * <p>Actua como <em>Composition Root</em>: es el unico lugar del programa
 * donde se instancian las implementaciones concretas y se inyectan en los
 * orquestadores. El resto del codigo solo conoce interfaces.</p>
 *
 * <p>Para cambiar la estrategia de almacenamiento del indice basta con
 * sustituir {@code new JsonIndexStorage()} por otra implementacion de
 * {@link InvertedIndexStorage} (MongoIndexStorage, FolderIndexStorage...).</p>
 */
public class ControlLayer {

    private static final Path CONTROL_PATH   = Paths.get("../control");
    private static final Path DOWNLOADS_FILE = CONTROL_PATH.resolve("downloaded_books.txt");
    private static final Path INDEXINGS_FILE = CONTROL_PATH.resolve("indexed_books.txt");
    private static final int  TOTAL_BOOKS    = 70000;

    // ── Composition Root: seleccionar estrategia de almacenamiento ────────────
    // Para benchmarks basta con cambiar esta linea:
    //   new MongoIndexStorage()
    //   new FolderIndexStorage()
    private static final InvertedIndexStorage storage = new JsonIndexStorage();
    private static final InvertedIndexBuilder indexer = new InvertedIndexBuilder(storage);

    // -------------------------------------------------------------------------

    private static Set<String> loadState(Path filePath) throws IOException {
        if (!Files.exists(filePath)) return new HashSet<>();
        return new HashSet<>(Files.readAllLines(filePath));
    }

    private static void appendState(Path filePath, String bookId) throws IOException {
        Files.writeString(filePath, bookId + System.lineSeparator(),
                          StandardOpenOption.CREATE, StandardOpenOption.APPEND);
    }

    public static void runPipelineStep() {
        try {
            Files.createDirectories(CONTROL_PATH);
            Set<String> downloaded = loadState(DOWNLOADS_FILE);
            Set<String> indexed    = loadState(INDEXINGS_FILE);

            Set<String> readyToIndex = new HashSet<>(downloaded);
            readyToIndex.removeAll(indexed);

            if (!readyToIndex.isEmpty()) {
                String bookIdStr = readyToIndex.iterator().next();
                int bookId = Integer.parseInt(bookIdStr);
                System.out.println("[CONTROL] Scheduling book " + bookId + " for processing...");

                MetadataExtractor.processBook(bookId);
                indexer.indexBook(bookId);
                indexer.saveIndex();

                appendState(INDEXINGS_FILE, bookIdStr);
                System.out.println("[CONTROL] Book " + bookId + " successfully processed and logged.");

            } else {
                Random random = new Random();
                for (int i = 0; i < 10; i++) {
                    int candidateId       = random.nextInt(TOTAL_BOOKS) + 1;
                    String candidateIdStr = String.valueOf(candidateId);

                    if (!downloaded.contains(candidateIdStr)) {
                        System.out.println("[CONTROL] Downloading new book with ID " + candidateId + "...");
                        String url = "https://www.gutenberg.org/cache/epub/" + candidateId
                                   + "/pg" + candidateId + ".txt";

                        boolean success = Downloader.downloadBook(candidateId, url);
                        if (success) {
                            appendState(DOWNLOADS_FILE, candidateIdStr);
                            System.out.println("[CONTROL] Book " + candidateId
                                    + " successfully logged as downloaded.");
                        }
                        break;
                    }
                }
            }
        } catch (Exception e) {
            System.err.println("[CONTROL] Pipeline step failed.");
            e.printStackTrace();
        }
    }

    public static void main(String[] args) {
        System.out.println("--- Starting Data Pipeline Step ---");
        runPipelineStep();
        System.out.println("--- Pipeline Step Finished ---");
    }
}