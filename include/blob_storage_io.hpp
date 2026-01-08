#pragma once

#include "blob_storage.hpp"

#include <unordered_map>
#include <list>
#include <mutex>
#include <future>
#include <functional>
#include <memory>

namespace blobstore {

// ---------------------------------------------------------------------------
// LRU Cache for read-heavy workloads (reduces syscall overhead)
// ---------------------------------------------------------------------------
class LRUCache {
public:
    explicit LRUCache(std::size_t maxBytes);

    // Returns nullptr if not cached.
    std::shared_ptr<std::vector<unsigned char>> get(const std::string& key);

    // Insert/update; may evict old entries.
    void put(const std::string& key, std::shared_ptr<std::vector<unsigned char>> data);

    // Invalidate key (e.g., after remove/put).
    void invalidate(const std::string& key);

    // Clear entire cache.
    void clear();

    std::size_t currentBytes() const { return currentBytes_; }
    std::size_t maxBytes() const { return maxBytes_; }

private:
    std::size_t maxBytes_;
    std::size_t currentBytes_ = 0;

    // LRU order: front = most recent
    std::list<std::string> order_;
    struct Entry {
        std::shared_ptr<std::vector<unsigned char>> data;
        std::list<std::string>::iterator it;
    };
    std::unordered_map<std::string, Entry> map_;

    mutable std::mutex mu_;

    void evictIfNeeded();
};

// ---------------------------------------------------------------------------
// CachedBlobStorage: wraps BlobStorage with an LRU read cache
// ---------------------------------------------------------------------------
class CachedBlobStorage {
public:
    CachedBlobStorage(std::string root, std::size_t cacheBytes);

    void init() { store_.init(); }

    void put(const std::string& key, const std::vector<unsigned char>& data);
    std::vector<unsigned char> get(const std::string& key);
    bool remove(const std::string& key);
    bool exists(const std::string& key) const { return store_.exists(key); }
    std::vector<std::string> list() const { return store_.list(); }
    std::size_t sizeOf(const std::string& key) const { return store_.sizeOf(key); }

    // Direct access
    BlobStorage& storage() { return store_; }
    LRUCache& cache() { return cache_; }

private:
    BlobStorage store_;
    LRUCache cache_;
};

// ---------------------------------------------------------------------------
// Batch / Async helpers for I/O-bound throughput
// ---------------------------------------------------------------------------

// Result of a single batch operation.
struct BatchResult {
    std::string key;
    bool success = false;
    std::string error;
};

// Batch put: writes multiple blobs; returns results.
std::vector<BatchResult> batchPut(
    BlobStorage& store,
    const std::vector<std::pair<std::string, std::vector<unsigned char>>>& items);

// Batch get: reads multiple blobs; missing keys have success=false.
std::vector<std::pair<std::string, std::vector<unsigned char>>> batchGet(
    BlobStorage& store,
    const std::vector<std::string>& keys);

// Async put: returns a future that completes when write is done.
std::future<void> asyncPut(
    BlobStorage& store,
    const std::string& key,
    std::vector<unsigned char> data);

// Async get: returns future with data.
std::future<std::vector<unsigned char>> asyncGet(
    BlobStorage& store,
    const std::string& key);

// ---------------------------------------------------------------------------
// Memory-mapped read (zero-copy for large blobs)
// ---------------------------------------------------------------------------
class MappedBlob {
public:
    MappedBlob() = default;
    ~MappedBlob();
    MappedBlob(const MappedBlob&) = delete;
    MappedBlob& operator=(const MappedBlob&) = delete;
    MappedBlob(MappedBlob&& o) noexcept;
    MappedBlob& operator=(MappedBlob&& o) noexcept;

    // Open a blob by key; throws on error.
    static MappedBlob open(const BlobStorage& store, const std::string& key);

    const unsigned char* data() const { return data_; }
    std::size_t size() const { return size_; }
    bool valid() const { return data_ != nullptr; }

private:
    unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
    int fd_ = -1;

    void close();
};

} // namespace blobstore
