#include "ControlLayer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <curl/curl.h>

namespace fs = std::filesystem;

// libcurl write callback to accumulate response into std::string.
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

void ControlLayer::resolvePaths(const std::string& controlDir, const std::string& datalakeDir, const std::string& datamartDir) {
    if (!controlDir.empty() && !datalakeDir.empty() && !datamartDir.empty()) {
        controlPath = fs::absolute(controlDir);
        datalakePath = fs::absolute(datalakeDir);
        datamartPath = fs::absolute(datamartDir);
    } else {
        // Automatically find project root by searching upwards for key landmark files
        fs::path root = fs::current_path();
        while (root.has_parent_path() && 
               !fs::exists(root / "stage_1_building_data_layer.pdf") && 
               !fs::exists(root / "README.md")) {
            root = root.parent_path();
        }

        controlPath = controlDir.empty() ? (root / "control") : fs::absolute(controlDir);
        datalakePath = datalakeDir.empty() ? (root / "data" / "datalake") : fs::absolute(datalakeDir);
        datamartPath = datamartDir.empty() ? (root / "data" / "datamarts") : fs::absolute(datamartDir);
    }

    downloadedFile = controlPath / "downloaded_books.txt";
    indexedFile = controlPath / "indexed_books.txt";

    std::error_code ec;
    fs::create_directories(controlPath, ec);
    fs::create_directories(datalakePath, ec);
    fs::create_directories(datamartPath, ec);
}

ControlLayer::ControlLayer(const std::string& controlDir,
                           const std::string& datalakeDir,
                           const std::string& datamartDir) {
    resolvePaths(controlDir, datalakeDir, datamartDir);
}

std::unordered_set<int> ControlLayer::readIds(const fs::path& filePath) {
    std::unordered_set<int> ids;
    std::ifstream file(filePath);
    if (!file.is_open()) return ids;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            try {
                ids.insert(std::stoi(line));
            } catch (...) {}
        }
    }
    return ids;
}

void ControlLayer::appendId(const fs::path& filePath, int bookId) {
    std::ofstream file(filePath, std::ios::app);
    if (file.is_open()) {
        file << bookId << "\n";
    }
}

bool ControlLayer::downloadBook(int bookId) {
    std::string url = "https://www.gutenberg.org/cache/epub/" + std::to_string(bookId) + "/pg" + std::to_string(bookId) + ".txt";

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[downloadBook] Failed to initialize CURL\n";
        return false;
    }

    std::string responseData;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BigData-SearchEngine/1.0");

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode != 200) {
        std::cerr << "[downloadBook] Book " << bookId << " unavailable (HTTP " << httpCode << ")\n";
        return false;
    }

    std::string startMarker = "*** START OF THE PROJECT GUTENBERG EBOOK";
    std::string endMarker = "*** END OF THE PROJECT GUTENBERG EBOOK";

    size_t startPos = responseData.find(startMarker);
    size_t endPos = responseData.find(endMarker);

    if (startPos == std::string::npos || endPos == std::string::npos || endPos <= startPos) {
        std::cerr << "[downloadBook] Gutenberg markers missing for book " << bookId << "\n";
        return false;
    }

    std::string header = responseData.substr(0, startPos);
    size_t bodyStart = responseData.find('\n', startPos);
    if (bodyStart == std::string::npos || bodyStart >= endPos) {
        bodyStart = startPos + startMarker.length();
    } else {
        bodyStart += 1;
    }
    std::string body = responseData.substr(bodyStart, endPos - bodyStart);

    return Datalake::saveBook(datalakePath, DatalakeLayout::TimeBased, bookId, header, body);
}

bool ControlLayer::ingestSampleBook(int bookId, const std::string& header, const std::string& body) {
    bool saved = Datalake::saveBook(datalakePath, DatalakeLayout::TimeBased, bookId, header, body);
    if (saved) {
        appendId(downloadedFile, bookId);
    }
    return saved;
}

bool ControlLayer::indexBook(int bookId) {
    BookFiles files = Datalake::findBookRecursive(datalakePath, bookId);
    if (!files.exists) {
        std::cerr << "[CONTROL] Could not locate files for book " << bookId << " in datalake.\n";
        return false;
    }

    // 1. Read header and body
    std::ifstream hStream(files.headerPath, std::ios::binary);
    std::string headerText((std::istreambuf_iterator<char>(hStream)), std::istreambuf_iterator<char>());

    std::ifstream bStream(files.bodyPath, std::ios::binary);
    std::string bodyText((std::istreambuf_iterator<char>(bStream)), std::istreambuf_iterator<char>());

    // 2. Parse metadata
    BookMetadata meta = MetadataExtractor::parseHeader(bookId, headerText);
    meta.headerPath = files.headerPath.string();
    meta.bodyPath = files.bodyPath.string();

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tmStruct{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tmStruct, &now_c);
#else
    localtime_r(&now_c, &tmStruct);
#endif
    std::ostringstream timeStream;
    timeStream << std::put_time(&tmStruct, "%Y-%m-%d %H:%M:%S");
    meta.ingestedAt = timeStream.str();

    // 3. Save metadata into SQLite database and CSV
    fs::path dbPath = datamartPath / "metadata.db";
    fs::path csvPath = datamartPath / "metadata.csv";
    MetadataExtractor::saveToDatabase(meta, dbPath.string());
    MetadataExtractor::saveToCsv(meta, csvPath.string());

    // 4. Update the 3 Inverted Index structures
    fs::path jsonPath = datamartPath / "inverted_index.json";
    fs::path hierDir  = datamartPath / "inverted_index_hier";
    fs::path binPath  = datamartPath / "inverted_index.bin";

    InvertedIndex::updateMonolithicIndex(bookId, bodyText, jsonPath.string());
    InvertedIndex::updateHierarchicalIndex(bookId, bodyText, hierDir.string());
    InvertedIndex::updateBinaryIndex(bookId, bodyText, binPath.string());

    std::cout << "[INDEXER] Successfully indexed book " << bookId 
              << " (\"" << meta.title << "\" by " << meta.author << ") across all datamarts.\n";
    return true;
}

bool ControlLayer::step() {
    auto downloaded = readIds(downloadedFile);
    auto indexed = readIds(indexedFile);

    std::vector<int> readyToIndex;
    for (int id : downloaded) {
        if (indexed.find(id) == indexed.end()) {
            readyToIndex.push_back(id);
        }
    }

    if (!readyToIndex.empty()) {
        std::sort(readyToIndex.begin(), readyToIndex.end());
        int bookId = readyToIndex.front();
        std::cout << "[CONTROL] Scheduling book " << bookId << " for indexing...\n";
        if (indexBook(bookId)) {
            appendId(indexedFile, bookId);
            std::cout << "[CONTROL] Book " << bookId << " marked as indexed.\n";
            return true;
        } else {
            std::cerr << "[CONTROL] Indexing failed for book " << bookId << ".\n";
            return false;
        }
    } else {
        // Pick random Gutenberg candidate not yet downloaded
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(1, 70000);

        for (int attempt = 0; attempt < 10; ++attempt) {
            int candidateId = distrib(gen);
            if (downloaded.find(candidateId) == downloaded.end()) {
                std::cout << "[CONTROL] Attempting to download book ID " << candidateId << "...\n";
                if (downloadBook(candidateId)) {
                    appendId(downloadedFile, candidateId);
                    std::cout << "[CONTROL] Book " << candidateId << " downloaded into Datalake.\n";
                    return true;
                }
            }
        }
        std::cerr << "[CONTROL] No new book downloaded after retry attempts.\n";
        return false;
    }
}

void ControlLayer::run(int steps) {
    for (int i = 0; i < steps; ++i) {
        std::cout << "\n--- Pipeline Step " << (i + 1) << " / " << steps << " ---\n";
        step();
    }
}
