import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.Statement;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

public class MetadataExtractor {

    private static final String DB_URL = "jdbc:sqlite:../data/datamarts/metadata.db";

    public static class BookMetadata {
        public int bookId;
        public String title;
        public String author;
        public String language;

        public BookMetadata(int bookId, String title, String author, String language) {
            this.bookId = bookId;
            this.title = title;
            this.author = author;
            this.language = language;
        }
    }

    private static void initDatabase() {
        String createTableSQL = "CREATE TABLE IF NOT EXISTS books ("
                + "book_id INTEGER PRIMARY KEY, "
                + "title TEXT, "
                + "author TEXT, "
                + "language TEXT"
                + ");";

        try {
            // 1. Crear la estructura de carpetas ANTES de la conexión
            Files.createDirectories(Paths.get("../data/datamarts"));
            
            // 2. Conectar a SQLite (esto creará el archivo metadata.db automáticamente)
            try (Connection conn = DriverManager.getConnection(DB_URL);
                 Statement stmt = conn.createStatement()) {
                
                stmt.execute(createTableSQL);
                System.out.println("[METADATA] Database and schema validated.");
            }
        } catch (Exception e) {
            System.err.println("[METADATA] Error initializing database: " + e.getMessage());
        }
    }

    private static void insertMetadata(BookMetadata metadata) {
        String insertSQL = "INSERT OR REPLACE INTO books (book_id, title, author, language) VALUES (?, ?, ?, ?)";

        try (Connection conn = DriverManager.getConnection(DB_URL);
             PreparedStatement pstmt = conn.prepareStatement(insertSQL)) {
            
            pstmt.setInt(1, metadata.bookId);
            pstmt.setString(2, metadata.title);
            pstmt.setString(3, metadata.author);
            pstmt.setString(4, metadata.language);
            pstmt.executeUpdate();
            
            System.out.println("[METADATA] Successfully inserted book " + metadata.bookId + " into database.");
            
        } catch (Exception e) {
            System.err.println("[METADATA] Error inserting into database: " + e.getMessage());
        }
    }

    private static Path findHeaderFile(int bookId) throws IOException {
        Path datalakePath = Paths.get("../data/datalake/");
        if (!Files.exists(datalakePath)) return null;

        try (Stream<Path> paths = Files.walk(datalakePath)) {
            return paths.filter(Files::isRegularFile)
                        .filter(p -> p.getFileName().toString().equals(bookId + ".header.txt"))
                        .findFirst()
                        .orElse(null);
        }
    }

    private static String extractField(String text, String regex) {
        Pattern pattern = Pattern.compile(regex, Pattern.CASE_INSENSITIVE);
        Matcher matcher = pattern.matcher(text);
        if (matcher.find()) {
            return matcher.group(1).trim();
        }
        return "Unknown";
    }

    public static void processBook(int bookId) {
        try {
            Path headerPath = findHeaderFile(bookId);
            if (headerPath == null) {
                System.out.println("[METADATA] Error: No header file found for book ID " + bookId);
                return;
            }

            String content = Files.readString(headerPath);
            String title = extractField(content, "Title:\\s*(.*)");
            String author = extractField(content, "Author:\\s*(.*)");
            String language = extractField(content, "Language:\\s*(.*)");

            BookMetadata metadata = new BookMetadata(bookId, title, author, language);
            insertMetadata(metadata);

        } catch (Exception e) {
            System.err.println("[METADATA] Failed to process metadata for book " + bookId);
            e.printStackTrace();
        }
    }
}