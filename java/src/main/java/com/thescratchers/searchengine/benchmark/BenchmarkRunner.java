package com.thescratchers.searchengine.benchmark;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Punto de entrada principal del benchmark comparativo entre lenguajes.
 *
 * <p>Lee la configuracion compartida desde la carpeta {@code shared/} del
 * repositorio y coordina la ejecucion de los distintos escenarios de prueba
 * para las escalas obligatorias: 1.000, 10.000 y 100.000 libros.</p>
 *
 * <p><b>Formato de los archivos de configuracion:</b>
 * <ul>
 *   <li>Lineas que comienzan por {@code #} son comentarios y se ignoran.</li>
 *   <li>Lineas en blanco tambien se ignoran.</li>
 *   <li>{@code books.txt}: un ID entero por linea (libros reales de Gutenberg).</li>
 *   <li>{@code words.txt}: una palabra por linea (workload de 10 terminos).</li>
 * </ul>
 * </p>
 *
 * <p><b>Rutas relativas</b> (resolucion desde el directorio de trabajo del proceso,
 * que debe ser la raiz del proyecto {@code stage_1/java/}):
 * <pre>
 *   ../../shared/books.txt
 *   ../../shared/words.txt
 * </pre>
 * </p>
 */
public class BenchmarkRunner {

    /** Escalas obligatorias del benchmark, en orden ascendente. */
    public static final int[] SCALES = {1_000, 10_000, 100_000};

    /** Ruta al archivo de IDs de libros reales, relativa al directorio de trabajo. */
    private static final Path BOOKS_PATH = Paths.get("../../shared/books.txt");

    /** Ruta al archivo de palabras del workload, relativa al directorio de trabajo. */
    private static final Path WORDS_PATH = Paths.get("../../shared/words.txt");

    // -------------------------------------------------------------------------
    // Carga de configuracion
    // -------------------------------------------------------------------------

    /**
     * Lee un archivo de configuracion y devuelve las lineas activas
     * (sin comentarios ni lineas en blanco).
     *
     * @param path ruta al archivo
     * @return lista inmutable de lineas activas, en el orden del archivo
     * @throws IOException si el archivo no existe o no puede leerse
     */
    public static List<String> loadLines(Path path) throws IOException {
        if (!Files.exists(path)) {
            throw new IOException("[BENCHMARK] Config file not found: " + path.toAbsolutePath());
        }

        List<String> result = new ArrayList<>();
        for (String line : Files.readAllLines(path)) {
            String trimmed = line.trim();
            // Ignorar comentarios y lineas vacias
            if (trimmed.isEmpty() || trimmed.startsWith("#")) continue;
            result.add(trimmed);
        }
        return Collections.unmodifiableList(result);
    }

    /**
     * Lee {@code books.txt} y devuelve los IDs de libros reales como enteros.
     *
     * <p>Las lineas que no sean parseable como enteros se saltan con un aviso
     * en lugar de lanzar excepcion, para mayor robustez.</p>
     *
     * @return lista inmutable de IDs de libro
     * @throws IOException si el archivo no puede leerse
     */
    public static List<Integer> loadBookIds() throws IOException {
        return loadBookIds(BOOKS_PATH);
    }

    /**
     * Sobrecarga que acepta una ruta personalizada (util para tests).
     *
     * @param path ruta al archivo {@code books.txt}
     * @return lista inmutable de IDs de libro
     * @throws IOException si el archivo no puede leerse
     */
    public static List<Integer> loadBookIds(Path path) throws IOException {
        List<String> lines = loadLines(path);
        List<Integer> ids  = new ArrayList<>(lines.size());

        for (String line : lines) {
            try {
                ids.add(Integer.parseInt(line));
            } catch (NumberFormatException e) {
                System.err.println("[BENCHMARK] Skipping non-integer line in books.txt: \"" + line + "\"");
            }
        }

        System.out.println("[BENCHMARK] Loaded " + ids.size() + " book IDs from " + path);
        return Collections.unmodifiableList(ids);
    }

    /**
     * Lee {@code words.txt} y devuelve exactamente las primeras 10 palabras
     * del workload (el benchmark siempre trabaja con 10 terminos).
     *
     * @return lista inmutable de palabras (max 10)
     * @throws IOException si el archivo no puede leerse
     */
    public static List<String> loadWords() throws IOException {
        return loadWords(WORDS_PATH);
    }

    /**
     * Sobrecarga que acepta una ruta personalizada (util para tests).
     *
     * @param path ruta al archivo {@code words.txt}
     * @return lista inmutable de palabras (max 10)
     * @throws IOException si el archivo no puede leerse
     */
    public static List<String> loadWords(Path path) throws IOException {
        List<String> lines = loadLines(path);

        if (lines.size() > 10) {
            System.err.println("[BENCHMARK] words.txt has " + lines.size()
                    + " entries; only the first 10 will be used.");
            lines = lines.subList(0, 10);
        }

        System.out.println("[BENCHMARK] Loaded " + lines.size() + " words from " + path + ": " + lines);
        return Collections.unmodifiableList(lines);
    }

    // -------------------------------------------------------------------------
    // Punto de entrada
    // -------------------------------------------------------------------------

    /**
     * Ejecuta el benchmark completo para todas las escalas obligatorias.
     *
     * @param args argumentos de linea de comandos (no se usan actualmente)
     */
    public static void main(String[] args) {
        System.out.println("=============================================================");
        System.out.println("  Search Engine Benchmark — Java implementation");
        System.out.println("=============================================================");

        try {
            // 1. Cargar configuracion
            List<Integer> bookIds = loadBookIds();
            List<String>  words   = loadWords();

            if (bookIds.isEmpty()) {
                System.err.println("[BENCHMARK] No book IDs found. Aborting.");
                return;
            }
            if (words.isEmpty()) {
                System.err.println("[BENCHMARK] No words found. Aborting.");
                return;
            }

            // 2. Ejecutar para cada escala obligatoria
            for (int scale : SCALES) {
                System.out.println("\n--- Scale: " + scale + " books ---");

                long t0 = System.currentTimeMillis();
                List<Integer> syntheticIds = SyntheticDataGenerator.generateScale(bookIds, scale);
                long elapsed = System.currentTimeMillis() - t0;

                System.out.printf("[BENCHMARK] Scale %,d ready in %d ms (%d synthetic IDs generated)%n",
                        scale, elapsed, syntheticIds.size() - bookIds.size());

                // TODO Stage-2: construir indice + ejecutar busquedas + medir tiempos
            }

            System.out.println("\n=============================================================");
            System.out.println("  Benchmark finished.");
            System.out.println("=============================================================");

        } catch (Exception e) {
            System.err.println("[BENCHMARK] Fatal error: " + e.getMessage());
            e.printStackTrace();
        }
    }
}