#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include "Datalake.hpp"
#include "MetadataExtractor.hpp"
#include "JsonIndex.hpp"
#include "HierarchicalIndex.hpp"
#include "BinaryIndex.hpp"

// Coordinates data ingestion from Project Gutenberg into the Datalake
// and indexing into Datamarts (SQLite, Monolithic JSON, Hierarchical, Binary Compact).
class ControlLayer {
public:
    // Resolves directories automatically based on execution context (root or build dir).
    ControlLayer(
        const std::string& controlDir = "",
        const std::string& datalakeDir = "",
        const std::string& datamartDir = ""
    );

    // Executes a single pipeline step: index pending book, or download a new one.
    bool step();

    // Runs N steps sequentially.
    void run(int steps);

    // Manually ingests an already available book (e.g. from sample dataset).
    bool ingestSampleBook(int bookId, const std::string& header, const std::string& body);

    // Getters for paths
    std::filesystem::path getControlPath() const { return controlPath; }
    std::filesystem::path getDatalakePath() const { return datalakePath; }
    std::filesystem::path getDatamartPath() const { return datamartPath; }

    // Reads control IDs from file.
    static std::unordered_set<int> readIds(const std::filesystem::path& filePath);

    // Appends an ID to a control file.
    static void appendId(const std::filesystem::path& filePath, int bookId);

private:
    std::filesystem::path controlPath;
    std::filesystem::path downloadedFile;
    std::filesystem::path indexedFile;
    std::filesystem::path datalakePath;
    std::filesystem::path datamartPath;

    // Downloads a book from Project Gutenberg and stores it in the Datalake.
    bool downloadBook(int bookId);

    // Indexes a book from Datalake into Datamarts (SQLite + 3 Inverted Index structures).
    bool indexBook(int bookId);

    // Resolves appropriate base directories automatically.
    void resolvePaths(const std::string& controlDir, const std::string& datalakeDir, const std::string& datamartDir);
};
