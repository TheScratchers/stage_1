#pragma once

#include <string>
#include <vector>
#include <unordered_set>

// Orchestrates the pipeline: downloads books from Gutenberg into the datalake
// and indexes them into the datamart. Each step either indexes a pending book
// or downloads a new one, never both.
class ControlLayer {
public:
    // Sets up directory paths and creates them if they don't exist.
    ControlLayer(const std::string& controlDir = "../control",
                 const std::string& datalakeDir = "../data/datalake",
                 const std::string& datamartDir = "../data/datamarts");

    // Runs a single pipeline step: index one pending book, or download a new one.
    void step();

    // Runs step() N times sequentially.
    void run(int steps);

private:
    std::string controlPath;    // Path to the control directory
    std::string downloadedFile; // Tracks downloaded book IDs (one per line)
    std::string indexedFile;    // Tracks indexed book IDs (one per line)
    std::string datalakePath;   // Root of the datalake directory
    std::string datamartPath;   // Root of the datamart directory

    // Reads a control file and returns all IDs as an unordered_set for O(1) lookup.
    std::unordered_set<int> readIds(const std::string& filePath);

    // Appends a single book ID to a control file.
    void appendId(const std::string& filePath, int bookId);

    // Downloads a book from Gutenberg, splits it into header/body, and saves
    // both under datalake/YYYYMMDD/HH/<bookId>.{header,body}.txt
    bool downloadBook(int bookId);

    // Locates book files in the datalake, extracts metadata into the datamart,
    // and builds the inverted indexes (monolithic JSON and hierarchical folders).
    bool indexBook(int bookId);
};
