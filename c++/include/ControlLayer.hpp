#pragma once

#include <string>
#include <vector>
#include <unordered_set>

class ControlLayer {
public:
    ControlLayer(const std::string& controlDir = "../control",
                 const std::string& datalakeDir = "../data/datalake");

    // Runs a single step: index pending books or download a new one
    void step();

    // Runs multiple steps
    void run(int steps);

private:
    std::string controlPath;
    std::string downloadedFile;
    std::string indexedFile;
    std::string datalakePath;

    std::unordered_set<int> readIds(const std::string& filePath);
    void appendId(const std::string& filePath, int bookId);
    bool downloadBook(int bookId);
    bool indexBook(int bookId);
};
