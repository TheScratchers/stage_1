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
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class SqliteIndexStorage implements InvertedIndexStorage {

    private static final String DB_PATH = "data/datamarts/inverted_index.db";
    private static final String DB_URL = "jdbc:sqlite:" + DB_PATH;
    private static final int BATCH_SIZE = 10000;

    private static final String DDL =
            "CREATE TABLE IF NOT EXISTS inverted_index ("
            + "term TEXT NOT NULL, "
            + "book_id INTEGER NOT NULL, "
            + "PRIMARY KEY (term, book_id)"
            + ");";

    private static final String INSERT =
            "INSERT OR IGNORE INTO inverted_index (term, book_id) VALUES (?, ?)";

    private static final String SELECT =
            "SELECT book_id FROM inverted_index WHERE term = ? ORDER BY book_id";

    private Connection writeConnection;
    private PreparedStatement writeStatement;
    private int batchCount = 0;

    public SqliteIndexStorage() {
        initSchema(DB_URL);
        initPersistentConnection();
    }

    public SqliteIndexStorage(String dbUrl) {
        initSchema(dbUrl);
        initPersistentConnection();
    }

    private void initPersistentConnection() {
        try {
            writeConnection = DriverManager.getConnection(DB_URL);
            try (Statement stmt = writeConnection.createStatement()) {
                stmt.execute("PRAGMA synchronous = OFF");
                stmt.execute("PRAGMA journal_mode = MEMORY");
                stmt.execute("PRAGMA temp_store = MEMORY");
            }
            writeConnection.setAutoCommit(false);
            writeStatement = writeConnection.prepareStatement(INSERT);
        } catch (SQLException e) {
            System.err.println(e.getMessage());
        }
    }

    @Override
    public void save(int bookId, List<String> terms) {
        if (terms == null || terms.isEmpty()) {
            return;
        }

        Set<String> uniqueTerms = new HashSet<>(terms);
        try {
            for (String term : uniqueTerms) {
                writeStatement.setString(1, term);
                writeStatement.setInt(2, bookId);
                writeStatement.addBatch();
                batchCount++;

                if (batchCount >= BATCH_SIZE) {
                    writeStatement.executeBatch();
                    writeConnection.commit();
                    writeStatement.clearBatch();
                    batchCount = 0;
                }
            }
            writeStatement.executeBatch();
            writeConnection.commit();
            writeStatement.clearBatch();
            batchCount = 0;
        } catch (SQLException e) {
            try {
                writeConnection.rollback();
            } catch (SQLException rollbackEx) {
                System.err.println(rollbackEx.getMessage());
            }
            System.err.println(e.getMessage());
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
            System.err.println(e.getMessage());
        }
        return Collections.unmodifiableList(result);
    }

    private void initSchema(String url) {
        try {
            Files.createDirectories(Paths.get(DB_PATH).getParent());
            try (Connection conn = DriverManager.getConnection(url);
                 Statement stmt = conn.createStatement()) {
                stmt.execute("PRAGMA synchronous = OFF");
                stmt.execute("PRAGMA journal_mode = MEMORY");
                stmt.execute("PRAGMA temp_store = MEMORY");
                stmt.execute(DDL);
            }
        } catch (IOException | SQLException e) {
            System.err.println(e.getMessage());
        }
    }
}