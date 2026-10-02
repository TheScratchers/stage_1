package com.thescratchers.searchengine.index;

import com.thescratchers.searchengine.datamarts.TextTokenizer;
import com.thescratchers.searchengine.datamarts.index.InvertedIndexStorage;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.stream.Stream;

/**
 * Orquestador del indice invertido.
 *
 * <p>Responsabilidades:
 * <ol>
 *   <li>Leer el cuerpo de un libro del datalake.</li>
 *   <li>Tokenizar mediante {@link TextTokenizer} (contrato compartido con el benchmark).</li>
 *   <li>Delegar la persistencia y la busqueda a un {@link InvertedIndexStorage}
 *       inyectado por constructor (<em>Dependency Inversion Principle</em>).</li>
 * </ol>
 * </p>
 */
public class InvertedIndexBuilder {

    private static final String DATALAKE_PATH = "../data/datalake/";

    /** Almacenamiento en memoria: termino -> conjunto ordenado de IDs. */
    private final Map<String, TreeSet<Integer>> invertedIndex = new TreeMap<>();

    /** Estrategia de persistencia inyectada. */
    private final InvertedIndexStorage storage;

    /**
     * Constructor principal.
     *
     * @param storage implementacion concreta del almacenamiento
     *                (JsonIndexStorage, MongoIndexStorage, FolderIndexStorage...)
     */
    public InvertedIndexBuilder(InvertedIndexStorage storage) {
        this.storage = storage;
    }

    // -------------------------------------------------------------------------
    // API publica
    // -------------------------------------------------------------------------

    /**
     * Tokeniza el cuerpo del libro con {@link TextTokenizer} e incorpora
     * los terminos al indice en memoria.
     *
     * @param bookId ID numerico del libro (Project Gutenberg)
     */
    public void indexBook(int bookId) {
        try {
            Path bodyPath = findBodyFile(bookId);
            if (bodyPath == null) {
                System.out.println("[INDEXER] Error: No body file found for book ID " + bookId);
                return;
            }

            String content = Files.readString(bodyPath);
            List<String> tokens = TextTokenizer.tokenize(content);

            for (String token : tokens) {
                invertedIndex.computeIfAbsent(token, k -> new TreeSet<>()).add(bookId);
            }

            System.out.println("[INDEXER] Successfully processed book " + bookId
                    + " (" + invertedIndex.size() + " unique terms so far)");

        } catch (Exception e) {
            System.err.println("[INDEXER] Failed to index book " + bookId);
            e.printStackTrace();
        }
    }

    /**
     * Persiste el indice en memoria utilizando la estrategia inyectada.
     * Convierte internamente {@code TreeSet<Integer>} a {@code List<Integer>}
     * para respetar el contrato de la interfaz.
     */
    public void saveIndex() {
        Map<String, List<Integer>> serializable = new TreeMap<>();
        for (Map.Entry<String, TreeSet<Integer>> entry : invertedIndex.entrySet()) {
            serializable.put(entry.getKey(), new ArrayList<>(entry.getValue()));
        }
        storage.save(serializable);
    }

    /**
     * Busca un termino en el almacenamiento persistido.
     *
     * @param term termino a buscar
     * @return lista de IDs de libros que contienen el termino
     */
    public List<Integer> search(String term) {
        return storage.search(term.toLowerCase());
    }

    // -------------------------------------------------------------------------
    // Privado
    // -------------------------------------------------------------------------

    private Path findBodyFile(int bookId) throws IOException {
        Path datalake = Paths.get(DATALAKE_PATH);
        if (!Files.exists(datalake)) return null;

        try (Stream<Path> paths = Files.walk(datalake)) {
            return paths.filter(Files::isRegularFile)
                        .filter(p -> p.getFileName().toString().equals(bookId + ".body.txt"))
                        .findFirst()
                        .orElse(null);
        }
    }
}