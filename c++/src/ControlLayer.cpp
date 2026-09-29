#include "ControlLayer.hpp"
#include <iostream>
#include <fstream>
#include <random>
#include <chrono>
#include <curl/curl.h>

namespace fs = std::filesystem;

static size_t curlWrite(void* ptr, size_t sz, size_t nm, void* stream) {
    static_cast<std::string*>(stream)->append(static_cast<char*>(ptr), sz * nm);
    return sz * nm;
}

void ControlLayer::resolvePaths(const std::string& cDir, const std::string& lDir, const std::string& mDir) {
    if (!cDir.empty() && !lDir.empty() && !mDir.empty()) {
        controlPath = fs::absolute(cDir);
        datalakePath = fs::absolute(lDir);
        datamartPath = fs::absolute(mDir);
    } else {
        fs::path root = fs::current_path();
        while (root.has_parent_path() && !fs::exists(root / "stage_1_building_data_layer.pdf") && !fs::exists(root / ".git")) {
            root = root.parent_path();
        }
        controlPath = cDir.empty() ? (root / "control") : fs::absolute(cDir);
        datalakePath = lDir.empty() ? (root / "data" / "datalake") : fs::absolute(lDir);
        datamartPath = mDir.empty() ? (root / "data" / "datamarts") : fs::absolute(mDir);
    }
    downloadedFile = controlPath / "downloaded_books.txt";
    indexedFile = controlPath / "indexed_books.txt";
    std::error_code ec;
    fs::create_directories(controlPath, ec);
    fs::create_directories(datalakePath, ec);
    fs::create_directories(datamartPath, ec);
}

ControlLayer::ControlLayer(const std::string& c, const std::string& l, const std::string& m) {
    resolvePaths(c, l, m);
}

std::unordered_set<int> ControlLayer::readIds(const fs::path& p) {
    std::unordered_set<int> ids;
    std::ifstream f(p);
    int id;
    while (f >> id) ids.insert(id);
    return ids;
}

void ControlLayer::appendId(const fs::path& p, int id) {
    std::ofstream f(p, std::ios::app);
    if (f.is_open()) f << id << "\n";
}

bool ControlLayer::downloadBook(int id) {
    std::string url = "https://www.gutenberg.org/cache/epub/" + std::to_string(id) + "/pg" + std::to_string(id) + ".txt";
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string resp;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BigData-Stage1/1.0");

    long code = 0;
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || code != 200) return false;
    size_t s = resp.find("*** START OF THE PROJECT GUTENBERG");
    size_t e = resp.find("*** END OF THE PROJECT GUTENBERG");
    if (s == std::string::npos || e == std::string::npos || e <= s) return false;

    std::string header = resp.substr(0, s);
    size_t bodyStart = resp.find('\n', s);
    std::string body = resp.substr(bodyStart == std::string::npos ? s : bodyStart + 1, e - bodyStart - 1);
    return Datalake::saveBook(datalakePath, DatalakeLayout::TimeBased, id, header, body);
}

bool ControlLayer::ingestSampleBook(int id, const std::string& h, const std::string& b) {
    if (Datalake::saveBook(datalakePath, DatalakeLayout::TimeBased, id, h, b)) {
        appendId(downloadedFile, id);
        return true;
    }
    return false;
}

bool ControlLayer::indexBook(int id) {
    BookFiles files = Datalake::findBookRecursive(datalakePath, id);
    if (!files.exists) return false;

    std::ifstream hs(files.headerPath, std::ios::binary);
    std::string hText((std::istreambuf_iterator<char>(hs)), std::istreambuf_iterator<char>());
    std::ifstream bs(files.bodyPath, std::ios::binary);
    std::string bText((std::istreambuf_iterator<char>(bs)), std::istreambuf_iterator<char>());

    BookMetadata meta = MetadataExtractor::parseHeader(id, hText);
    meta.headerPath = files.headerPath.string();
    meta.bodyPath = files.bodyPath.string();
    meta.ingestedAt = std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

    MetadataExtractor::saveToDatabase(meta, (datamartPath / "metadata.db").string());
    JsonIndex::update(id, bText, (datamartPath / "inverted_index.json").string());
    HierarchicalIndex::update(id, bText, (datamartPath / "inverted_index_hier").string());
    BinaryIndex::update(id, bText, (datamartPath / "inverted_index.bin").string());

    std::cout << "[INDEXER] Indexed book " << id << " (\"" << meta.title << "\" by " << meta.author << ")\n";
    return true;
}

bool ControlLayer::step() {
    auto dl = readIds(downloadedFile), idx = readIds(indexedFile);
    for (int id : dl) {
        if (idx.find(id) == idx.end()) {
            std::cout << "[CONTROL] Scheduling book " << id << " for indexing...\n";
            if (indexBook(id)) {
                appendId(indexedFile, id);
                std::cout << "[CONTROL] Book " << id << " indexed.\n";
                return true;
            }
            return false;
        }
    }
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(1, 60000);
    for (int i = 0; i < 10; ++i) {
        int id = dist(gen);
        if (dl.find(id) == dl.end() && downloadBook(id)) {
            appendId(downloadedFile, id);
            std::cout << "[CONTROL] Book " << id << " downloaded.\n";
            return true;
        }
    }
    return false;
}

void ControlLayer::run(int steps) {
    for (int i = 0; i < steps; ++i) {
        std::cout << "--- Pipeline Step " << (i + 1) << " / " << steps << " ---\n";
        step();
    }
}
