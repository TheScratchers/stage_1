package com.thescratchers.searchengine.datamarts.index;

import java.util.List;

public interface InvertedIndexStorage {

    void save(int bookId, List<String> terms);

    List<Integer> search(String term);
}