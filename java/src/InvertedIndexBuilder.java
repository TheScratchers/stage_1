import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.stream.Stream;

public class InvertedIndexBuilder {

    private static final String DATALAKE_PATH = "../data/datalake/";
    private static final String INDEX_PATH = "../data/datamarts/inverted_index.json";

    private static Map<String, Set<Integer>> invertedIndex = new TreeMap<>();

    private static Path findBodyFile(int bookId) throws IOException {
        Path datalake = Paths.get(DATALAKE_PATH);
        if (!Files.exists(datalake)) return null;

        try (Stream<Path> paths = Files.walk(datalake)) {
            return paths.filter(Files::isRegularFile)
                        .filter(p -> p.getFileName().toString().equals(bookId + ".body.txt"))
                        .findFirst()
                        .orElse(null);
        }
    }

    public static void indexBook(int bookId) {
        try {
            Path bodyPath = findBodyFile(bookId);
            if (bodyPath == null) {
                System.out.println("[INDEXER] Error: No body file found for book ID " + bookId);
                return;
            }

            String content = Files.readString(bodyPath);
            
            String[] words = content.toLowerCase().split("[^a-z0-9]+");

            for (String word : words) {
                if (word.isEmpty()) continue;
                invertedIndex.computeIfAbsent(word, k -> new TreeSet<>()).add(bookId);
            }

            System.out.println("[INDEXER] Successfully processed book " + bookId);

        } catch (Exception e) {
            System.err.println("[INDEXER] Failed to index book " + bookId);
            e.printStackTrace();
        }
    }

    public static void saveIndex() {
        try {
            Files.createDirectories(Paths.get("../data/datamarts"));
            StringBuilder json = new StringBuilder("{\n");
            
            int wordCount = 0;
            for (Map.Entry<String, Set<Integer>> entry : invertedIndex.entrySet()) {
                if (wordCount > 0) json.append(",\n");
                
                json.append("  \"").append(entry.getKey()).append("\": [");
                
                int idCount = 0;
                for (Integer id : entry.getValue()) {
                    if (idCount > 0) json.append(", ");
                    json.append(id);
                    idCount++;
                }
                json.append("]");
                wordCount++;
            }
            json.append("\n}");

            Files.writeString(Paths.get(INDEX_PATH), json.toString());
            System.out.println("[INDEXER] Monolithic index saved to " + INDEX_PATH);
            System.out.println("[INDEXER] Total unique words indexed: " + wordCount);

        } catch (IOException e) {
            System.err.println("[INDEXER] Error saving index.");
            e.printStackTrace();
        }
    }
}