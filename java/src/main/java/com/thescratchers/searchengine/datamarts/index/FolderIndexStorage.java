package com.thescratchers.searchengine.datamarts.index;

import java.util.Collections;
import java.util.List;
import java.util.Map;

/**
 * Stub de {@link InvertedIndexStorage} que fragmenta el indice en archivos
 * individuales por termino dentro de una carpeta raiz.
 *
 * <p><b>Estado actual:</b> esqueleto listo para implementar en Stage 2.</p>
 *
 * <p><b>Diseno previsto:</b>
 * <ul>
 *   <li>Una carpeta raiz {@code ../data/datamarts/index/}.</li>
 *   <li>Un archivo por termino con nombre {@code <term>.txt},
 *       conteniendo los IDs de libro separados por saltos de linea.</li>
 *   <li>Subcarpetas por letra inicial para evitar directorios con millones
 *       de entradas: {@code index/a/apple.txt}, {@code index/b/book.txt}…</li>
 * </ul>
 * </p>
 *
 * <p>Esta estrategia es especialmente util para benchmarks de lectura
 * aleatoria, ya que cada busqueda accede a un unico archivo peque&ntilde;o.</p>
 */
public class FolderIndexStorage implements InvertedIndexStorage {

    private static final String DEFAULT_ROOT = "../data/datamarts/index/";

    private final String rootPath;

    /** Constructor con ruta por defecto. */
    public FolderIndexStorage() {
        this(DEFAULT_ROOT);
    }

    /** Constructor que permite inyectar la ruta raiz (util para tests). */
    public FolderIndexStorage(String rootPath) {
        this.rootPath = rootPath;
    }

    // -------------------------------------------------------------------------
    // InvertedIndexStorage
    // -------------------------------------------------------------------------

    @Override
    public void save(Map<String, List<Integer>> index) {
        // TODO Stage-2:
        //   for each entry:
        //     Path dir  = Paths.get(rootPath, entry.getKey().substring(0,1));
        //     Path file = dir.resolve(entry.getKey() + ".txt");
        //     Files.createDirectories(dir);
        //     Files.writeString(file, ids joined by newlines);
        System.out.println("[FOLDER-STORAGE] save() not yet implemented — stub.");
        System.out.println("[FOLDER-STORAGE] Would write " + index.size()
                + " term-files under " + rootPath);
    }

    @Override
    public List<Integer> search(String term) {
        // TODO Stage-2:
        //   Path file = Paths.get(rootPath, term.substring(0,1), term + ".txt");
        //   if (!Files.exists(file)) return emptyList();
        //   return Files.readAllLines(file).stream().map(Integer::parseInt).toList();
        System.out.println("[FOLDER-STORAGE] search() not yet implemented — stub. term=" + term);
        return Collections.emptyList();
    }
}