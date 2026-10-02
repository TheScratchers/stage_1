package com.thescratchers.searchengine.datamarts.index;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;

/**
 * Implementacion de {@link InvertedIndexStorage} que persiste el indice
 * como un unico archivo JSON monolitico en el sistema de ficheros local.
 *
 * <p>Estrategia de escritura: serializa manualmente el mapa a JSON para
 * evitar dependencias extra; en etapas posteriores se puede sustituir por
 * Jackson {@code ObjectMapper} si se prefiere.</p>
 */
public class JsonIndexStorage implements InvertedIndexStorage {

    private static final String DEFAULT_PATH = "../data/datamarts/inverted_index.json";

    private final Path outputPath;

    /** Constructor con ruta por defecto. */
    public JsonIndexStorage() {
        this(Paths.get(DEFAULT_PATH));
    }

    /** Constructor que permite inyectar la ruta (util para tests). */
    public JsonIndexStorage(Path outputPath) {
        this.outputPath = outputPath;
    }

    // -------------------------------------------------------------------------
    // InvertedIndexStorage
    // -------------------------------------------------------------------------

    @Override
    public void save(Map<String, List<Integer>> index) {
        try {
            Files.createDirectories(outputPath.getParent());

            StringBuilder json = new StringBuilder("{\n");
            int wordCount = 0;

            for (Map.Entry<String, List<Integer>> entry : index.entrySet()) {
                if (wordCount > 0) json.append(",\n");

                json.append("  \"").append(entry.getKey()).append("\": [");

                List<Integer> ids = entry.getValue();
                for (int i = 0; i < ids.size(); i++) {
                    if (i > 0) json.append(", ");
                    json.append(ids.get(i));
                }
                json.append("]");
                wordCount++;
            }
            json.append("\n}");

            Files.writeString(outputPath, json.toString());
            System.out.println("[JSON-STORAGE] Index saved to " + outputPath);
            System.out.println("[JSON-STORAGE] Total unique terms: " + wordCount);

        } catch (IOException e) {
            System.err.println("[JSON-STORAGE] Error saving index: " + e.getMessage());
            e.printStackTrace();
        }
    }

    @Override
    public List<Integer> search(String term) {
        if (!Files.exists(outputPath)) {
            System.err.println("[JSON-STORAGE] Index file not found: " + outputPath);
            return Collections.emptyList();
        }
        try {
            String content = Files.readString(outputPath);
            // Simple lookup: find "term": [id1, id2, ...]
            String key = "\"" + term.toLowerCase() + "\": [";
            int start = content.indexOf(key);
            if (start == -1) return Collections.emptyList();

            int arrayStart = content.indexOf('[', start) + 1;
            int arrayEnd   = content.indexOf(']', arrayStart);
            String raw = content.substring(arrayStart, arrayEnd).trim();

            if (raw.isEmpty()) return Collections.emptyList();

            List<Integer> result = new ArrayList<>();
            for (String part : raw.split(",")) {
                String trimmed = part.trim();
                if (!trimmed.isEmpty()) {
                    result.add(Integer.parseInt(trimmed));
                }
            }
            return result;

        } catch (IOException e) {
            System.err.println("[JSON-STORAGE] Error reading index: " + e.getMessage());
            return Collections.emptyList();
        }
    }
}