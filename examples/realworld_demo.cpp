// realworld_demo.cpp: Comprehensive real-world usage demo for fluxor-blob
// Demonstrates: Media CDN, Document Store, Log Aggregation, Backup, ML Registry
// Usage: g++ -std=c++17 -O2 -o realworld_demo realworld_demo.cpp -lpthread -lssl -lcrypto

#include "blob_storage.hpp"
#include "blob_storage_io.hpp"
#include "blob_indexer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <future>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <set>
#include <atomic>
#include <thread>
#include <fstream>
#include <ctime>
#include <openssl/sha.h>

// --- Media CDN Backend ---
class MediaStorage {
public:
    MediaStorage(const std::string& root, size_t cacheMB = 512)
        : storage_(root, cacheMB * 1024 * 1024) { storage_.init(); }
    std::string upload(const std::vector<unsigned char>& data, const std::string& userId, const std::string& ext) {
        auto timestamp = std::time(nullptr);
        auto hash = std::hash<std::string>{}(std::string(data.begin(), data.end()));
        std::string key = userId + "/" + std::to_string(timestamp) + "_" + std::to_string(hash) + "." + ext;
        storage_.put(key, data);
        return key;
    }
    std::vector<unsigned char> serve(const std::string& key) { return storage_.get(key); }
    std::vector<std::string> listUserMedia(const std::string& userId) {
        auto all = storage_.list();
        std::vector<std::string> result;
        std::string prefix = userId + "/";
        for (const auto& k : all) if (k.substr(0, prefix.length()) == prefix) result.push_back(k);
        return result;
    }
private:
    blobstore::CachedBlobStorage storage_;
};

// --- Document Store ---
class DocumentStore {
public:
    DocumentStore(const std::string& root) : store_(root) { store_.init(); }
    void putDoc(const std::string& collection, const std::string& docId, const std::string& doc) {
        std::string key = collection + "/" + docId;
        std::vector<unsigned char> data(doc.begin(), doc.end());
        store_.put(key, data);
    }
    std::string getDoc(const std::string& collection, const std::string& docId) {
        std::string key = collection + "/" + docId;
        auto data = store_.get(key);
        return std::string(data.begin(), data.end());
    }
    std::vector<std::string> listCollection(const std::string& collection) {
        return store_.keysWithPrefix(collection + "/");
    }
private:
    blobstore::IndexedBlobStorage store_;
};

// --- Log Aggregation ---
class LogStore {
public:
    LogStore(const std::string& root) : store_(root) { store_.init(); }
    void log(const std::string& service, const std::string& level, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        time_t t = ts;
        struct tm* tm_info = std::gmtime(&t);
        char date[16];
        strftime(date, sizeof(date), "%Y%m%d", tm_info);
        std::string key = service + "/" + date + "/" + level + "_" + std::to_string(ts);
        std::string entry = std::to_string(ts) + " [" + level + "] " + service + ": " + message + "\n";
        std::vector<unsigned char> data(entry.begin(), entry.end());
        store_.put(key, data);
    }
    std::vector<std::string> getServiceLogs(const std::string& service, const std::string& date) {
        auto all = store_.list();
        std::vector<std::string> keys;
        std::string prefix = service + "/" + date + "/";
        for (const auto& k : all) if (k.substr(0, prefix.length()) == prefix) keys.push_back(k);
        auto results = blobstore::batchGet(store_.storage(), keys);
        std::vector<std::string> logs;
        for (const auto& [key, data] : results) logs.emplace_back(data.begin(), data.end());
        return logs;
    }
private:
    blobstore::CachedBlobStorage store_;
};

// --- Backup System ---
class BackupStore {
public:
    BackupStore(const std::string& root) : store_(root) { store_.init(); }
    std::string backup(const std::string& filepath) {
        std::ifstream ifs(filepath, std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(data.data(), data.size(), hash);
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        std::string contentHash = ss.str();
        if (!store_.exists(contentHash)) store_.put(contentHash, data);
        return contentHash;
    }
    void restore(const std::string& contentHash, const std::string& outpath) {
        auto data = store_.get(contentHash);
        std::ofstream ofs(outpath, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    std::vector<std::string> backupBatch(const std::vector<std::string>& files) {
        std::vector<std::future<std::string>> futures;
        for (const auto& f : files) futures.push_back(std::async(std::launch::async, [this, f]() { return this->backup(f); }));
        std::vector<std::string> hashes;
        for (auto& fut : futures) hashes.push_back(fut.get());
        return hashes;
    }
private:
    blobstore::BlobStorage store_;
};

// --- ML Model Registry ---
class ModelRegistry {
public:
    ModelRegistry(const std::string& root) : store_(root) { store_.init(); }
    void registerModel(const std::string& modelName, const std::string& version, const std::vector<unsigned char>& weights, const std::map<std::string, std::string>& metadata) {
        std::string weightsKey = modelName + "/" + version + "/weights";
        store_.put(weightsKey, weights);
        std::string metaKey = modelName + "/" + version + "/metadata";
        std::string metaJson = serializeMetadata(metadata);
        std::vector<unsigned char> metaData(metaJson.begin(), metaJson.end());
        store_.put(metaKey, metaData);
    }
    std::vector<std::string> listVersions(const std::string& modelName) {
        auto all = store_.list();
        std::set<std::string> versions;
        std::string prefix = modelName + "/";
        for (const auto& k : all) {
            if (k.substr(0, prefix.length()) == prefix) {
                auto parts = split(k, '/');
                if (parts.size() >= 2) versions.insert(parts[1]);
            }
        }
        return std::vector<std::string>(versions.begin(), versions.end());
    }
private:
    blobstore::CachedBlobStorage store_;
    std::string serializeMetadata(const std::map<std::string, std::string>& meta) {
        std::stringstream ss; ss << "{"; bool first = true;
        for (const auto& [k, v] : meta) { if (!first) ss << ","; ss << "\"" << k << "\":\"" << v << "\""; first = false; }
        ss << "}"; return ss.str();
    }
    std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> result; std::stringstream ss(s); std::string item;
        while (std::getline(ss, item, delim)) result.push_back(item);
        return result;
    }
};

// --- Main Demo ---
int main() {
    std::cout << "=== Media CDN ===\n";
    MediaStorage media("/tmp/realworld_media");
    std::vector<unsigned char> img = {'a','b','c'};
    std::string url = media.upload(img, "user123", "jpg");
    std::cout << "Uploaded: " << url << "\n";
    auto img2 = media.serve(url);
    std::cout << "Served size: " << img2.size() << "\n";

    std::cout << "\n=== Document Store ===\n";
    DocumentStore docs("/tmp/realworld_docs");
    docs.putDoc("users", "alice", "{\"name\":\"Alice\"}");
    std::string alice = docs.getDoc("users", "alice");
    std::cout << "Alice doc: " << alice << "\n";

    std::cout << "\n=== Log Aggregation ===\n";
    LogStore logs("/tmp/realworld_logs");
    logs.log("web", "INFO", "Started");
    logs.log("web", "ERROR", "Failed");
    auto today = logs.getServiceLogs("web", "20260108");
    std::cout << "Log count: " << today.size() << "\n";

    std::cout << "\n=== Backup System ===\n";
    BackupStore backup("/tmp/realworld_backup");
    std::ofstream("/tmp/realworld_file.txt") << "hello world";
    std::string hash = backup.backup("/tmp/realworld_file.txt");
    std::cout << "Backup hash: " << hash << "\n";
    backup.restore(hash, "/tmp/realworld_restored.txt");

    std::cout << "\n=== ML Model Registry ===\n";
    ModelRegistry registry("/tmp/realworld_models");
    std::vector<unsigned char> weights = {1,2,3,4};
    registry.registerModel("sentiment", "v1.0", weights, {{"acc","0.95"}});
    auto versions = registry.listVersions("sentiment");
    std::cout << "Model versions: ";
    for (const auto& v : versions) std::cout << v << " ";
    std::cout << "\n";

    return 0;
}
