#pragma once

#include <string>
#include <unordered_set>
#include <filesystem>
#include "Datalake.hpp"
#include "MetadataExtractor.hpp"
#include "JsonIndex.hpp"
#include "HierarchicalIndex.hpp"
#include "SqliteIndex.hpp"

// Orchestrates downloading, datalake storage, and datamart indexing.
class ControlLayer {
public:
    ControlLayer(const std::string& cDir = "", const std::string& lDir = "", const std::string& mDir = "");
    bool step();
    void run(int steps);
    bool ingestSampleBook(int bookId, const std::string& header, const std::string& body);
    void syncDatamarts();
    bool indexBook(int bookId);

    std::filesystem::path getControlPath() const { return controlPath; }
    std::filesystem::path getDatalakePath() const { return datalakePath; }
    std::filesystem::path getDatamartPath() const { return datamartPath; }

    static std::unordered_set<int> readIds(const std::filesystem::path& filePath);
    static void appendId(const std::filesystem::path& filePath, int bookId);

private:
    std::filesystem::path controlPath, downloadedFile, indexedFile, datalakePath, datamartPath;
    bool downloadBook(int bookId);
    void resolvePaths(const std::string& cDir, const std::string& lDir, const std::string& mDir);
};
