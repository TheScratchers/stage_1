package com.thescratchers.searchengine.datamarts.index;

import java.util.List;
import java.util.Map;

/**
 * Abstraccion de almacenamiento del indice invertido (DIP).
 *
 * <p>Cualquier estrategia de persistencia (JSON, MongoDB, carpetas...)
 * debe implementar este contrato. InvertedIndexBuilder depende de esta
 * interfaz, no de una implementacion concreta.</p>
 */
public interface InvertedIndexStorage {

    /**
     * Persiste el indice completo.
     *
     * @param index mapa termino -> lista de IDs de libro que lo contienen
     */
    void save(Map<String, List<Integer>> index);

    /**
     * Recupera la lista de IDs de libros que contienen el termino dado.
     *
     * @param term termino de busqueda (minusculas, sin acentos)
     * @return lista de IDs, o lista vacia si el termino no existe
     */
    List<Integer> search(String term);
}