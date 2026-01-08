#include "blob_indexer.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <thread>
#include <atomic>

namespace blobstore {

// ---------------------------------------------------------------------------
// FastBlobIndexer
// ---------------------------------------------------------------------------

FastBlobIndexer::FastBlobIndexer(BlobStorage& store) : store_(store) {}

std::string FastBlobIndexer::indexFilePath() const {
    return store_.root() + "/.blob_index";
}

std::uint64_t FastBlobIndexer::nowTimestamp() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

void FastBlobIndexer::rebuild(const std::string& bucket) {
    std::lock_guard<std::mutex> lk(mu_);
    hashIndex_.clear();
    sortedIndex_.clear();

    auto keys = store_.list(bucket);
    std::vector<std::pair<std::string, BlobMeta>> metas(keys.size());
    unsigned nT = std::min(8u, std::thread::hardware_concurrency());
    std::atomic<size_t> idx{0};
    std::vector<std::thread> ths;
    for (unsigned t = 0; t < nT; ++t) {
        ths.emplace_back([&]() {
            for (;;) {
                size_t s = idx.fetch_add(1024);
                if (s >= keys.size()) break;
                size_t e = std::min(s + 1024, keys.size());
                for (size_t i = s; i < e; ++i) {
                    BlobMeta meta;
                    try {
                        meta.size = store_.sizeOf(bucket, keys[i]);
                        meta.modTime = nowTimestamp();
                    } catch (...) {
                        meta.size = 0; meta.modTime = 0;
                    }
                    metas[i] = {keys[i], meta};
                }
            }
        });
    }
    for (auto& th : ths) th.join();
    for (size_t i = 0; i < metas.size(); ++i) {
        if (metas[i].second.size == 0 && metas[i].second.modTime == 0) continue;
        hashIndex_[metas[i].first] = metas[i].second;
    }
    // Build sorted index (pointers into hash index).
    for (auto& [key, meta] : hashIndex_) {
        sortedIndex_[key] = &meta;
    }
}

bool FastBlobIndexer::loadFromFile() {
    std::lock_guard<std::mutex> lk(mu_);
    std::ifstream ifs(indexFilePath(), std::ios::binary);
    if (!ifs) return false;

    hashIndex_.clear();
    sortedIndex_.clear();

    std::string line;
    while (std::getline(ifs, line)) {
        // Format: key\tsize\tmodTime
        auto tab1 = line.find('\t');
        if (tab1 == std::string::npos) continue;
        auto tab2 = line.find('\t', tab1 + 1);
        if (tab2 == std::string::npos) continue;

        std::string key = line.substr(0, tab1);
        std::size_t size = std::stoull(line.substr(tab1 + 1, tab2 - tab1 - 1));
        std::uint64_t modTime = std::stoull(line.substr(tab2 + 1));

        BlobMeta meta{size, modTime};
        hashIndex_[key] = meta;
    }

    for (auto& [key, meta] : hashIndex_) {
        sortedIndex_[key] = &meta;
    }

    return !hashIndex_.empty() || ifs.eof();
}

void FastBlobIndexer::saveToFile() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::ofstream ofs(indexFilePath(), std::ios::binary | std::ios::trunc);
    if (!ofs) return;

    for (const auto& [key, meta] : hashIndex_) {
        ofs << key << '\t' << meta.size << '\t' << meta.modTime << '\n';
    }
}

void FastBlobIndexer::onPut(const std::string& key, std::size_t size) {
    std::lock_guard<std::mutex> lk(mu_);
    BlobMeta meta{size, nowTimestamp()};
    hashIndex_[key] = meta;
    sortedIndex_[key] = &hashIndex_[key];
}

void FastBlobIndexer::onRemove(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    sortedIndex_.erase(key);
    hashIndex_.erase(key);
}

bool FastBlobIndexer::exists(const std::string& key) const {
    std::lock_guard<std::mutex> lk(mu_);
    return hashIndex_.find(key) != hashIndex_.end();
}

std::optional<BlobMeta> FastBlobIndexer::getMeta(const std::string& key) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = hashIndex_.find(key);
    if (it == hashIndex_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> FastBlobIndexer::allKeys() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> keys;
    keys.reserve(sortedIndex_.size());
    for (const auto& [key, _] : sortedIndex_) {
        keys.push_back(key);
    }
    return keys;
}

std::size_t FastBlobIndexer::count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return hashIndex_.size();
}

std::size_t FastBlobIndexer::totalBytes() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::size_t total = 0;
    for (const auto& [_, meta] : hashIndex_) {
        total += meta.size;
    }
    return total;
}

std::vector<std::string> FastBlobIndexer::keysWithPrefix(const std::string& prefix) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> result;

    auto it = sortedIndex_.lower_bound(prefix);
    while (it != sortedIndex_.end()) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) break;
        result.push_back(it->first);
        ++it;
    }
    return result;
}

std::vector<std::string> FastBlobIndexer::keysInRange(const std::string& start, const std::string& end) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> result;

    auto itStart = sortedIndex_.lower_bound(start);
    auto itEnd = sortedIndex_.lower_bound(end);

    for (auto it = itStart; it != itEnd; ++it) {
        result.push_back(it->first);
    }
    return result;
}

void FastBlobIndexer::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    hashIndex_.clear();
    sortedIndex_.clear();
}

// ---------------------------------------------------------------------------
// IndexedBlobStorage
// ---------------------------------------------------------------------------

IndexedBlobStorage::IndexedBlobStorage(std::string root, std::string bucket)
    : bucket_(std::move(bucket)), store_(std::move(root)), indexer_(store_) {}

void IndexedBlobStorage::init() {
    store_.init(bucket_);
    // Try to load persisted index; rebuild if missing.
    if (!indexer_.loadFromFile()) {
        indexer_.rebuild(bucket_);
    }
}

void IndexedBlobStorage::put(const std::string& key, const std::vector<unsigned char>& data) {
    store_.put(bucket_, key, data);
    indexer_.onPut(key, data.size());
}

std::vector<unsigned char> IndexedBlobStorage::get(const std::string& key) const {
    return store_.get(bucket_, key);
}

bool IndexedBlobStorage::remove(const std::string& key) {
    bool ok = store_.remove(bucket_, key);
    if (ok) {
        indexer_.onRemove(key);
    }
    return ok;
}

} // namespace blobstore
