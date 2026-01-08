#include "blob_storage_io.hpp"

#include <stdexcept>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>

namespace blobstore {

// ---------------------------------------------------------------------------
// LRUCache
// ---------------------------------------------------------------------------

LRUCache::LRUCache(std::size_t maxBytes) : maxBytes_(maxBytes) {}

std::shared_ptr<std::vector<unsigned char>> LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return nullptr;
    // Move to front (most recent)
    order_.erase(it->second.it);
    order_.push_front(key);
    it->second.it = order_.begin();
    return it->second.data;
}

void LRUCache::put(const std::string& key, std::shared_ptr<std::vector<unsigned char>> data) {
    std::lock_guard<std::mutex> lk(mu_);
    invalidate(key); // remove old if exists (unlocked call ok since we hold lock)
    std::size_t sz = data ? data->size() : 0;
    order_.push_front(key);
    map_[key] = {data, order_.begin()};
    currentBytes_ += sz;
    evictIfNeeded();
}

void LRUCache::invalidate(const std::string& key) {
    auto it = map_.find(key);
    if (it == map_.end()) return;
    currentBytes_ -= it->second.data ? it->second.data->size() : 0;
    order_.erase(it->second.it);
    map_.erase(it);
}

void LRUCache::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    map_.clear();
    order_.clear();
    currentBytes_ = 0;
}

void LRUCache::evictIfNeeded() {
    while (currentBytes_ > maxBytes_ && !order_.empty()) {
        const std::string& oldest = order_.back();
        auto it = map_.find(oldest);
        if (it != map_.end()) {
            currentBytes_ -= it->second.data ? it->second.data->size() : 0;
            map_.erase(it);
        }
        order_.pop_back();
    }
}

// ---------------------------------------------------------------------------
// CachedBlobStorage
// ---------------------------------------------------------------------------

CachedBlobStorage::CachedBlobStorage(std::string root, std::size_t cacheBytes)
    : store_(std::move(root)), cache_(cacheBytes) {}

void CachedBlobStorage::put(const std::string& key, const std::vector<unsigned char>& data) {
    std::string bucket = "default";
    store_.put(bucket, key, data);
    cache_.invalidate(key); // invalidate stale cache
}

std::vector<unsigned char> CachedBlobStorage::get(const std::string& key) {
    auto cached = cache_.get(key);
    if (cached) return *cached;
    std::string bucket = "default";
    auto data = store_.get(bucket, key);
    cache_.put(key, std::make_shared<std::vector<unsigned char>>(data));
    return data;
}

bool CachedBlobStorage::remove(const std::string& key) {
    cache_.invalidate(key);
    std::string bucket = "default";
    return store_.remove(bucket, key);
}

// ---------------------------------------------------------------------------
// Batch operations
// ---------------------------------------------------------------------------

std::vector<BatchResult> batchPut(
    BlobStorage& store,
    const std::vector<std::pair<std::string, std::vector<unsigned char>>>& items)
{
    std::vector<BatchResult> results;
    results.reserve(items.size());
    std::string bucket = "default";
    for (const auto& [key, data] : items) {
        BatchResult r;
        r.key = key;
        try {
            store.put(bucket, key, data);
            r.success = true;
        } catch (const std::exception& ex) {
            r.success = false;
            r.error = ex.what();
        }
        results.push_back(std::move(r));
    }
    return results;
}

std::vector<std::pair<std::string, std::vector<unsigned char>>> batchGet(
    BlobStorage& store,
    const std::vector<std::string>& keys)
{
    std::vector<std::pair<std::string, std::vector<unsigned char>>> results;
    results.reserve(keys.size());
    std::string bucket = "default";
    for (const auto& key : keys) {
        try {
            results.emplace_back(key, store.get(bucket, key));
        } catch (...) {
            results.emplace_back(key, std::vector<unsigned char>{});
        }
    }
    return results;
}

// ---------------------------------------------------------------------------
// Async helpers (simple thread-based; production should use thread pool)
// ---------------------------------------------------------------------------

std::future<void> asyncPut(
    BlobStorage& store,
    const std::string& key,
    std::vector<unsigned char> data)
{
    return std::async(std::launch::async, [&store, key, data = std::move(data)]() mutable {
        std::string bucket = "default";
        store.put(bucket, key, data);
    });
}

std::future<std::vector<unsigned char>> asyncGet(
    BlobStorage& store,
    const std::string& key)
{
    return std::async(std::launch::async, [&store, key]() {
        std::string bucket = "default";
        return store.get(bucket, key);
    });
}

// ---------------------------------------------------------------------------
// MappedBlob (memory-mapped read)
// ---------------------------------------------------------------------------

MappedBlob::~MappedBlob() { close(); }

MappedBlob::MappedBlob(MappedBlob&& o) noexcept
    : data_(o.data_), size_(o.size_), fd_(o.fd_) {
    o.data_ = nullptr;
    o.size_ = 0;
    o.fd_ = -1;
}

MappedBlob& MappedBlob::operator=(MappedBlob&& o) noexcept {
    if (this != &o) {
        close();
        data_ = o.data_;
        size_ = o.size_;
        fd_ = o.fd_;
        o.data_ = nullptr;
        o.size_ = 0;
        o.fd_ = -1;
    }
    return *this;
}

void MappedBlob::close() {
    if (data_) {
        ::munmap(data_, size_);
        data_ = nullptr;
        size_ = 0;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// Helper to get path for a key (mirrors BlobStorage::pathForKey logic)
static std::string pathForKeyExternal(const BlobStorage& store, const std::string& key) {
    // Use the bucketed path logic from BlobStorage
    std::string bucket = "default";
    // Use the same logic as BlobStorage::pathForKey(bucket, key)
    // (no versionId, so latest version)
    // This is a public method, so we can call it directly
    return store.pathForKey(bucket, key);
}

MappedBlob MappedBlob::open(const BlobStorage& store, const std::string& key) {
    std::string path = pathForKeyExternal(store, key);
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("MappedBlob: failed to open " + path + ": " + std::strerror(errno));
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw std::runtime_error("MappedBlob: fstat failed: " + std::string(std::strerror(errno)));
    }
    std::size_t sz = static_cast<std::size_t>(st.st_size);
    void* ptr = nullptr;
    if (sz > 0) {
        ptr = ::mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd, 0);
        if (ptr == MAP_FAILED) {
            ::close(fd);
            throw std::runtime_error("MappedBlob: mmap failed: " + std::string(std::strerror(errno)));
        }
    }
    MappedBlob mb;
    mb.data_ = static_cast<unsigned char*>(ptr);
    mb.size_ = sz;
    mb.fd_ = fd;
    return mb;
}

} // namespace blobstore
