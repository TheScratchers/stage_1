package com.thescratchers.searchengine.datamarts.index;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.StringJoiner;

/**
 * Implementacion de {@link InvertedIndexStorage} que persiste el indice
 * invertido en una base de datos SQLite local.
 *
 * <p><b>Esquema:</b>
 * <pre>
 * CREATE TABLE inverted_index (
 *     term     TEXT PRIMARY KEY,
 *     book_ids TEXT NOT NULL   -- IDs separados por comas, e.g. "1,42,300"
 * );
 * </pre>
 * </p>
 *
 * <p><b>Decisiones de diseno:</b>
 * <ul>
 *   <li>Los IDs se almacenan como CSV en una columna TEXT para minimizar el
 *       numero de filas y simplificar las lecturas de busqueda.</li>
 *   <li>{@code INSERT OR REPLACE} garantiza idempotencia: si el termino ya
 *       existe, se sobreescribe con la lista actualizada.</li>
 *   <li>La inicializacion de la tabla se realiza en el constructor, por lo que
 *       la base de datos esta lista desde el primer uso.</li>
 * </ul>
 * </p>
 *
 * <p>Dependencia en pom.xml: {@code org.xerial:sqlite-jdbc}</p>
 */
public class SqliteIndexStorage implements InvertedIndexStorage {

    private static final String DEFAULT_DB_URL = "jdbc:sqlite:../data/datamarts/inverted_index.db";

    private final String dbUrl;

    /** Constructor con ruta de base de datos por defecto. */
    public SqliteIndexStorage() {
        this(DEFAULT_DB_URL);
    }

    /**
     * Constructor que permite inyectar la URL JDBC (util para tests con base de
     * datos en memoria: {@code "jdbc:sqlite::memory:"}).
     *
     * @param dbUrl URL JDBC de SQLite
     */
    public SqliteIndexStorage(String dbUrl) {
        this.dbUrl = dbUrl;
        initDatabase();
    }

    // -------------------------------------------------------------------------
    // InvertedIndexStorage
    // -------------------------------------------------------------------------

    /**
     * Persiste el indice completo en SQLite.
     *
     * <p>Cada termino se guarda en una fila; sus IDs de libro se serializan
     * como una cadena CSV. La operacion {@code INSERT OR REPLACE} hace que la
     * llamada sea idempotente.</p>
     *
     * @param index mapa termino -> lista ordenada de IDs de libro
     */
    @Override
    public void save(Map<String, List<Integer>> index) {
        if (index.isEmpty()) return;

        String sql = "INSERT OR REPLACE INTO inverted_index (term, book_ids) VALUES (?, ?)";

        try (Connection conn = DriverManager.getConnection(dbUrl);
             PreparedStatement pstmt = conn.prepareStatement(sql)) {

            // Batch insert para mejor rendimiento
            conn.setAutoCommit(false);

            for (Map.Entry<String, List<Integer>> entry : index.entrySet()) {
                pstmt.setString(1, entry.getKey());
                pstmt.setString(2, idsToString(entry.getValue()));
                pstmt.addBatch();
            }

            pstmt.executeBatch();
            conn.commit();

            System.out.println("[SQLITE-STORAGE] Index saved: " + index.size() + " terms -> " + dbUrl);

        } catch (SQLException e) {
            System.err.println("[SQLITE-STORAGE] Error saving index: " + e.getMessage());
            e.printStackTrace();
        }
    }

    /**
     * Recupera los IDs de libros que contienen el termino dado.
     *
     * @param term termino de busqueda (se espera en minusculas)
     * @return lista de IDs de libro, o lista vacia si el termino no existe
     */
    @Override
    public List<Integer> search(String term) {
        String sql = "SELECT book_ids FROM inverted_index WHERE term = ?";

        try (Connection conn = DriverManager.getConnection(dbUrl);
             PreparedStatement pstmt = conn.prepareStatement(sql)) {

            pstmt.setString(1, term.toLowerCase());

            try (ResultSet rs = pstmt.executeQuery()) {
                if (rs.next()) {
                    return stringToIds(rs.getString("book_ids"));
                }
            }

        } catch (SQLException e) {
            System.err.println("[SQLITE-STORAGE] Error searching term '" + term + "': " + e.getMessage());
            e.printStackTrace();
        }

        return Collections.emptyList();
    }

    // -------------------------------------------------------------------------
    // Privado
    // -------------------------------------------------------------------------

    /**
     * Crea la tabla {@code inverted_index} si no existe todavia.
     * Se ejecuta una sola vez en el constructor.
     */
    private void initDatabase() {
        String ddl = "CREATE TABLE IF NOT EXISTS inverted_index ("
                + "term     TEXT PRIMARY KEY, "
                + "book_ids TEXT NOT NULL"
                + ");";

        try (Connection conn  = DriverManager.getConnection(dbUrl);
             Statement   stmt = conn.createStatement()) {

            stmt.execute(ddl);
            System.out.println("[SQLITE-STORAGE] Schema validated: " + dbUrl);

        } catch (SQLException e) {
            System.err.println("[SQLITE-STORAGE] Error initializing schema: " + e.getMessage());
            e.printStackTrace();
        }
    }

    /** Convierte una lista de IDs a cadena CSV: [1, 42, 300] -> "1,42,300". */
    private static String idsToString(List<Integer> ids) {
        StringJoiner joiner = new StringJoiner(",");
        for (Integer id : ids) {
            joiner.add(String.valueOf(id));
        }
        return joiner.toString();
    }

    /** Convierte una cadena CSV de IDs a lista: "1,42,300" -> [1, 42, 300]. */
    private static List<Integer> stringToIds(String csv) {
        if (csv == null || csv.isBlank()) return Collections.emptyList();

        String[] parts = csv.split(",");
        List<Integer> ids = new ArrayList<>(parts.length);
        for (String part : parts) {
            String trimmed = part.trim();
            if (!trimmed.isEmpty()) {
                ids.add(Integer.parseInt(trimmed));
            }
        }
        return Collections.unmodifiableList(ids);
    }
}