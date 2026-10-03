package com.thescratchers.searchengine.datamarts;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MetadataExtractor {

    private static final String DB_PATH = "data/datamarts/metadata.db";
    private static final String DB_URL  = "jdbc:sqlite:" + DB_PATH;

    private static final String DDL =
            "CREATE TABLE IF NOT EXISTS books ("
            + "book_id  INTEGER PRIMARY KEY, "
            + "title    TEXT, "
            + "author   TEXT, "
            + "language TEXT"
            + ");";

    private static final String UPSERT =
            "INSERT OR REPLACE INTO books (book_id, title, author, language) "
            + "VALUES (?, ?, ?, ?)";

    private static final Pattern TITLE_PATTERN =
            Pattern.compile("^Title:\\s*(.+)$", Pattern.MULTILINE | Pattern.CASE_INSENSITIVE);

    private static final Pattern AUTHOR_PATTERN =
            Pattern.compile("^Author:\\s*(.+)$", Pattern.MULTILINE | Pattern.CASE_INSENSITIVE);

    private static final Pattern LANGUAGE_PATTERN =
            Pattern.compile("^Language:\\s*(.+)$", Pattern.MULTILINE | Pattern.CASE_INSENSITIVE);

    public MetadataExtractor() {
        initSchema();
    }

    public void extractAndStoreMetadata(int bookId, String headerFilePath)
            throws IOException, SQLException {

        Path headerPath = Paths.get(headerFilePath);
        if (!Files.exists(headerPath)) {
            throw new IOException("Header file not found: " + headerPath.toAbsolutePath());
        }

        String content = Files.readString(headerPath);

        String title    = extractField(content, TITLE_PATTERN);
        String author   = extractField(content, AUTHOR_PATTERN);
        String language = extractField(content, LANGUAGE_PATTERN);

        upsert(bookId, title, author, language);

        System.out.println("[METADATA] Book " + bookId
                + " stored — title=\"" + title + "\""
                + ", author=\"" + author + "\""
                + ", language=\"" + language + "\"");
    }

    private void initSchema() {
        try {
            Files.createDirectories(Paths.get(DB_PATH).getParent());
            try (Connection conn = DriverManager.getConnection(DB_URL);
                 Statement  stmt = conn.createStatement()) {
                stmt.execute(DDL);
            }
        } catch (IOException | SQLException e) {
            System.err.println("[METADATA] Schema init failed: " + e.getMessage());
        }
    }

    private void upsert(int bookId, String title, String author, String language)
            throws SQLException {
        try (Connection conn = DriverManager.getConnection(DB_URL);
             PreparedStatement pstmt = conn.prepareStatement(UPSERT)) {
            pstmt.setInt(1, bookId);
            pstmt.setString(2, title);
            pstmt.setString(3, author);
            pstmt.setString(4, language);
            pstmt.executeUpdate();
        }
    }

    private static String extractField(String text, Pattern pattern) {
        Matcher matcher = pattern.matcher(text);
        return matcher.find() ? matcher.group(1).trim() : "Unknown";
    }
}