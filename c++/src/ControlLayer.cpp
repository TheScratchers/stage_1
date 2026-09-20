#include "ControlLayer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <curl/curl.h>

namespace fs = std::filesystem;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

ControlLayer::ControlLayer(const std::string& controlDir, const std::string& datalakeDir)
    : controlPath(controlDir),
      downloadedFile(controlDir + "/downloaded_books.txt"),
      indexedFile(controlDir + "/indexed_books.txt"),
      datalakePath(datalakeDir) {
    fs::create_directories(controlPath);
    fs::create_directories(datalakePath);
}

std::unordered_set<int> ControlLayer::readIds(const std::string& filePath) {
    std::unordered_set<int> ids;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return ids;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            try {
                ids.insert(std::stoi(line));
            } catch (...) {
            }
        }
    }
    return ids;
}

void ControlLayer::appendId(const std::string& filePath, int bookId) {
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BigData-SearchEngine/1.0");

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode != 200) {
        std::cerr << "[downloadBook] Failed to download book " << bookId << " (HTTP " << httpCode << ")\n";
        return false;
    }

    std::string startMarker = "*** START OF THE PROJECT GUTENBERG EBOOK";
    std::string endMarker = "*** END OF THE PROJECT GUTENBERG EBOOK";

    size_t startPos = responseData.find(startMarker);
    size_t endPos = responseData.find(endMarker);

    if (startPos == std::string::npos || endPos == std::string::npos || endPos <= startPos) {
        std::cerr << "[downloadBook] Gutenberg markers not found for book " << bookId << "\n";
        return false;
    }

    size_t headerEnd = startPos;
    std::string header = responseData.substr(0, headerEnd);

    size_t bodyStart = responseData.find('\n', startPos);
    if (bodyStart == std::string::npos || bodyStart >= endPos) {
        bodyStart = startPos + startMarker.length();
    } else {
        bodyStart += 1;
    }
    std::string body = responseData.substr(bodyStart, endPos - bodyStart);

    // Time-based directory layout: datalake/YYYYMMDD/HH/
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_struct{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&tm_struct, &now_c);
#else
    localtime_r(&now_c, &tm_struct);
#endif

    std::ostringstream folderStream;
    folderStream << datalakePath << "/"
                 << std::put_time(&tm_struct, "%Y%m%d/%H");
    
    fs::path targetFolder = folderStream.str();
    fs::create_directories(targetFolder);

    fs::path headerPath = targetFolder / (std::to_string(bookId) + ".header.txt");
    fs::path bodyPath = targetFolder / (std::to_string(bookId) + ".body.txt");

    std::ofstream hFile(headerPath, std::ios::binary);
    if (!hFile.is_open()) return false;
    hFile.write(header.data(), header.size());

    std::ofstream bFile(bodyPath, std::ios::binary);
    if (!bFile.is_open()) return false;
    bFile.write(body.data(), body.size());

    return true;
}

bool ControlLayer::indexBook(int bookId) {
    // Check if book files exist in datalake
    bool found = false;
    for (const auto& entry : fs::recursive_directory_iterator(datalakePath)) {
        if (entry.is_regular_file() && entry.path().filename() == (std::to_string(bookId) + ".body.txt")) {
            found = true;
            break;
        }
    }
    if (!found) {
        std::cerr << "[CONTROL] Could not locate files for book " << bookId << " in datalake.\n";
        return false;
    }

    // Placeholder indexing step for Stage 1 (mirrors pipeline indexing)
    std::cout << "[INDEXER] Indexed book " << bookId << " successfully.\n";
    return true;
}

void ControlLayer::step() {
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
        int bookId = readyToIndex[0];
        std::cout << "[CONTROL] Indexing book " << bookId << "...\n";
        if (indexBook(bookId)) {
            appendId(indexedFile, bookId);
            std::cout << "[CONTROL] Book " << bookId << " successfully indexed.\n";
        } else {
            std::cerr << "[CONTROL] Failed to index book " << bookId << ".\n";
        }
    } else {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(1, 70000);

        for (int attempt = 0; attempt < 10; ++attempt) {
            int candidateId = distrib(gen);
            if (downloaded.find(candidateId) == downloaded.end()) {
                std::cout << "[CONTROL] Downloading new book with ID " << candidateId << "...\n";
                if (downloadBook(candidateId)) {
                    appendId(downloadedFile, candidateId);
                    std::cout << "[CONTROL] Book " << candidateId << " successfully downloaded.\n";
                } else {
                    std::cerr << "[CONTROL] Failed to download book " << candidateId << ".\n";
                }
                break;
            }
        }
    }
}

void ControlLayer::run(int steps) {
    for (int i = 0; i < steps; ++i) {
        step();
    }
}
