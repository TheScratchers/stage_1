package com.thescratchers.searchengine.benchmark;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Genera colecciones de IDs de libros sinteticos para los benchmarks
 * de escalabilidad.
 *
 * <h2>Estrategia de generacion</h2>
 * <p>Los IDs sinteticos se calculan con la formula:</p>
 * <pre>
 *   syntheticId = SYNTHETIC_ID_OFFSET + round * baseBookIds.size() + posInBase
 * </pre>
 * <p>Esto garantiza que:</p>
 * <ul>
 *   <li>Los IDs sinteticos nunca colisionan con IDs reales de Project Gutenberg
 *       (que van de 1 a ~70.000).</li>
 *   <li>El patron de tokens en memoria sigue la distribucion de los libros
 *       reales, lo que hace el benchmark realista.</li>
 *   <li>La generacion es puramente aritmetica (sin I/O), por lo que el tiempo
 *       medido refleja el coste del indexado, no el de descarga.</li>
 * </ul>
 *
 * <h2>Escalas obligatorias</h2>
 * <pre>
 *   Scale     Total IDs   IDs sinteticos (con 20 reales)
 *   ─────     ─────────   ──────────────────────────────
 *    1.000     1.000        980
 *   10.000    10.000      9.980
 *  100.000   100.000     99.980
 * </pre>
 */
public final class SyntheticDataGenerator {

    /**
     * Base para los IDs sinteticos. Todos los IDs generados seran mayores que
     * este valor para no colisionar con IDs reales de Gutenberg (<= 70.000).
     */
    public static final int SYNTHETIC_ID_OFFSET = 1_000_000;

    /** Escalas obligatorias del benchmark. */
    public static final int[] MANDATORY_SCALES = BenchmarkRunner.SCALES;

    // Clase utilitaria; no se instancia.
    private SyntheticDataGenerator() {}

    // -------------------------------------------------------------------------
    // API publica
    // -------------------------------------------------------------------------

    /**
     * Genera una lista de IDs de libro que combina los IDs reales con IDs
     * sinteticos hasta alcanzar la escala indicada, midiendo y registrando
     * el tiempo de generacion por consola.
     *
     * <p>La lista resultante siempre tiene exactamente {@code targetScale}
     * elementos (o mas si {@code baseBookIds.size() >= targetScale}, en cuyo
     * caso se devuelven solo los reales truncados a {@code targetScale}).</p>
     *
     * @param baseBookIds IDs de libros reales (tipicamente los ~20 de Gutenberg)
     * @param targetScale numero total de libros deseado (e.g. 1000, 10000, 100000)
     * @return lista inmutable con {@code targetScale} IDs (reales + sinteticos)
     * @throws IllegalArgumentException si {@code targetScale < 1} o
     *         {@code baseBookIds} es null o vacio
     */
    public static List<Integer> generateScale(List<Integer> baseBookIds, int targetScale) {
        validateInput(baseBookIds, targetScale);

        System.out.printf("[SYNTHETIC] Starting generation for scale %,d (base: %d real books)...%n",
                targetScale, baseBookIds.size());

        long startTime = System.nanoTime();

        List<Integer> result = buildIdList(baseBookIds, targetScale);

        long elapsedMs = (System.nanoTime() - startTime) / 1_000_000;
        int syntheticCount = result.size() - Math.min(baseBookIds.size(), targetScale);

        System.out.printf("[SYNTHETIC] Scale %,d generated in %d ms "
                + "(real: %d, synthetic: %d, total: %d)%n",
                targetScale, elapsedMs,
                Math.min(baseBookIds.size(), targetScale),
                syntheticCount,
                result.size());

        return result;
    }

    /**
     * Ejecuta la generacion para todas las escalas obligatorias en secuencia,
     * registrando el tiempo acumulado y por escala.
     *
     * @param baseBookIds IDs de libros reales
     * @return lista de listas, una por cada escala en {@link #MANDATORY_SCALES}
     */
    public static List<List<Integer>> generateAllScales(List<Integer> baseBookIds) {
        System.out.println("[SYNTHETIC] ============================================");
        System.out.println("[SYNTHETIC] Running all mandatory scales...");
        System.out.println("[SYNTHETIC] ============================================");

        long globalStart = System.nanoTime();
        List<List<Integer>> results = new ArrayList<>(MANDATORY_SCALES.length);

        for (int scale : MANDATORY_SCALES) {
            results.add(generateScale(baseBookIds, scale));
        }

        long totalMs = (System.nanoTime() - globalStart) / 1_000_000;
        System.out.printf("[SYNTHETIC] All scales completed in %d ms total.%n", totalMs);
        System.out.println("[SYNTHETIC] ============================================");

        return Collections.unmodifiableList(results);
    }

    // -------------------------------------------------------------------------
    // Privado
    // -------------------------------------------------------------------------

    /**
     * Construye la lista de IDs combinando los reales con sinteticos.
     *
     * <p>Algoritmo:
     * <ol>
     *   <li>Copia los IDs reales al inicio de la lista.</li>
     *   <li>Si {@code baseBookIds.size() >= targetScale}, devuelve los primeros
     *       {@code targetScale} reales.</li>
     *   <li>Si no, rellena con IDs sinteticos calculados ciclicamente sobre la
     *       base hasta completar {@code targetScale} entradas.</li>
     * </ol>
     * </p>
     */
    private static List<Integer> buildIdList(List<Integer> baseBookIds, int targetScale) {
        List<Integer> result = new ArrayList<>(targetScale);

        // Paso 1: anadir los reales (hasta targetScale)
        int realCount = Math.min(baseBookIds.size(), targetScale);
        for (int i = 0; i < realCount; i++) {
            result.add(baseBookIds.get(i));
        }

        if (result.size() >= targetScale) {
            return Collections.unmodifiableList(result.subList(0, targetScale));
        }

        // Paso 2: generar sinteticos de forma ciclica sobre la base
        int base   = baseBookIds.size();
        int needed = targetScale - result.size();
        int round  = 0;
        int pos    = 0;

        for (int i = 0; i < needed; i++) {
            // ID sintetico: offset + vuelta * tam_base + posicion_en_base
            int synId = SYNTHETIC_ID_OFFSET + round * base + pos;
            result.add(synId);

            pos++;
            if (pos >= base) {
                pos = 0;
                round++;
            }
        }

        return Collections.unmodifiableList(result);
    }

    /** Valida los parametros de entrada y lanza excepciones descriptivas. */
    private static void validateInput(List<Integer> baseBookIds, int targetScale) {
        if (baseBookIds == null || baseBookIds.isEmpty()) {
            throw new IllegalArgumentException(
                    "[SYNTHETIC] baseBookIds must not be null or empty.");
        }
        if (targetScale < 1) {
            throw new IllegalArgumentException(
                    "[SYNTHETIC] targetScale must be >= 1, got: " + targetScale);
        }
    }
}