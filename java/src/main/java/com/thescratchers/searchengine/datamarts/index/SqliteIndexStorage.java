package com.thescratchers.searchengine.datamarts.index;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class SqliteIndexStorage implements InvertedIndexStorage {

    private static final String DB_PATH = "data/datamarts/inverted_index.db";
    private static final String DB_URL  = "jdbc:sqlite:" + DB_PATH;

    private static final String DDL =
            "CREATE TABLE IF NOT EXISTS inverted_index ("
            + "term    TEXT    NOT NULL, "
            + "book_id INTEGER NOT NULL, "
            + "PRIMARY KEY (term, book_id)"
            + ");";

    private static final String INSERT =
            "INSERT OR IGNORE INTO inverted_index (term, book_id) VALUES (?, ?)";

    private static final String SELECT =
            "SELECT book_id FROM inverted_index WHERE term = ? ORDER BY book_id";

    public SqliteIndexStorage() {
        initSchema();
    }

    public SqliteIndexStorage(String dbUrl) {
        initSchema(dbUrl);
    }

    @Override
    public void save(int bookId, List<String> terms) {
        if (terms == null || terms.isEmpty()) return;
        try (Connection conn = DriverManager.getConnection(DB_URL);
             PreparedStatement pstmt = conn.prepareStatement(INSERT)) {
            conn.setAutoCommit(false);
            for (String term : terms) {
                pstmt.setString(1, term);
                pstmt.setInt(2, bookId);
                pstmt.addBatch();
            }
            pstmt.executeBatch();
            conn.commit();
            System.out.println("[SQLITE-INDEX] Saved " + terms.size()
                    + " terms for book " + bookId);
        } catch (SQLException e) {
            System.err.println("[SQLITE-INDEX] Error saving terms for book "
                    + bookId + ": " + e.getMessage());
        }
    }

    @Override
    public List<Integer> search(String term) {
        List<Integer> result = new ArrayList<>();
        try (Connection conn = DriverManager.getConnection(DB_URL);
             PreparedStatement pstmt = conn.prepareStatement(SELECT)) {
            pstmt.setString(1, term.toLowerCase());
            try (ResultSet rs = pstmt.executeQuery()) {
                while (rs.next()) {
                    result.add(rs.getInt("book_id"));
                }
            }
        } catch (SQLException e) {
            System.err.println("[SQLITE-INDEX] Error searching term '"
                    + term + "': " + e.getMessage());
        }
        return Collections.unmodifiableList(result);
    }

    private void initSchema() {
        initSchema(DB_URL);
    }

    private void initSchema(String url) {
        try {
            Files.createDirectories(Paths.get(DB_PATH).getParent());
            try (Connection conn = DriverManager.getConnection(url);
                 Statement stmt = conn.createStatement()) {
                stmt.execute(DDL);
                System.out.println("[SQLITE-INDEX] Schema validated: " + url);
            }
        } catch (IOException | SQLException e) {
            System.err.println("[SQLITE-INDEX] Schema init failed: " + e.getMessage());
        }
    }
}