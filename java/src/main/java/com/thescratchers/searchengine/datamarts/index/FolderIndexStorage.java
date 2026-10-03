package com.thescratchers.searchengine.datamarts.index;

import java.util.Collections;
import java.util.List;

public class FolderIndexStorage implements InvertedIndexStorage {

    private static final String DEFAULT_ROOT = "data/datamarts/index/";

    private final String rootPath;

    public FolderIndexStorage() {
        this(DEFAULT_ROOT);
    }

    public FolderIndexStorage(String rootPath) {
        this.rootPath = rootPath;
    }

    @Override
    public void save(int bookId, List<String> terms) {
        System.out.println("[FOLDER-INDEX] save() not yet implemented — stub.");
        System.out.println("[FOLDER-INDEX] Would write " + terms.size()
                + " terms for book " + bookId + " under " + rootPath);
    }

    @Override
    public List<Integer> search(String term) {
        System.out.println("[FOLDER-INDEX] search() not yet implemented — stub. term=" + term);
        return Collections.emptyList();
    }
}