#pragma once

#include "blob_storage.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <mutex>
#include <optional>
#include <cstdint>

namespace blobstore {

// Metadata for an indexed blob.
struct BlobMeta {
    std::size_t size = 0;
    std::uint64_t modTime = 0; // Unix timestamp (seconds)
};

// ---------------------------------------------------------------------------
// FastBlobIndexer: in-memory index for O(1) key lookups and prefix queries.
// ---------------------------------------------------------------------------
class FastBlobIndexer {
public:
    explicit FastBlobIndexer(BlobStorage& store);

    // Rebuild index by scanning all blobs on disk. Call after init or if stale.
    void rebuild();

    // Load index from persistent file (fast startup). Returns false if missing/corrupt.
    bool loadFromFile();

    // Save index to persistent file.
    void saveToFile() const;

    // Index maintenance (call after put/remove to keep in sync).
    void onPut(const std::string& key, std::size_t size);
    void onRemove(const std::string& key);

    // Fast lookups (O(1) average).
    bool exists(const std::string& key) const;
    std::optional<BlobMeta> getMeta(const std::string& key) const;

    // Fast enumeration.
    std::vector<std::string> allKeys() const;
    std::size_t count() const;
    std::size_t totalBytes() const;

    // Prefix query: returns all keys starting with prefix, sorted.
    std::vector<std::string> keysWithPrefix(const std::string& prefix) const;

    // Range query: returns keys in [start, end), sorted.
    std::vector<std::string> keysInRange(const std::string& start, const std::string& end) const;

    // Clear the index (does not delete blobs).
    void clear();

private:
    BlobStorage& store_;

    // Primary hash index for O(1) lookup.
    std::unordered_map<std::string, BlobMeta> hashIndex_;

    // Sorted index for prefix/range queries.
    std::map<std::string, BlobMeta*> sortedIndex_;

    mutable std::mutex mu_;

    std::string indexFilePath() const;
    static std::uint64_t nowTimestamp();
};

// ---------------------------------------------------------------------------
// IndexedBlobStorage: BlobStorage with automatic index maintenance.
// ---------------------------------------------------------------------------
class IndexedBlobStorage {
public:
    explicit IndexedBlobStorage(std::string root);

    void init();

    // Standard operations (auto-update index).
    void put(const std::string& key, const std::vector<unsigned char>& data);
    std::vector<unsigned char> get(const std::string& key) const;
    bool remove(const std::string& key);

    // Fast indexed lookups.
    bool exists(const std::string& key) const { return indexer_.exists(key); }
    std::optional<BlobMeta> getMeta(const std::string& key) const { return indexer_.getMeta(key); }
    std::vector<std::string> list() const { return indexer_.allKeys(); }
    std::size_t count() const { return indexer_.count(); }
    std::size_t totalBytes() const { return indexer_.totalBytes(); }

    // Prefix/range queries.
    std::vector<std::string> keysWithPrefix(const std::string& prefix) const {
        return indexer_.keysWithPrefix(prefix);
    }
    std::vector<std::string> keysInRange(const std::string& start, const std::string& end) const {
        return indexer_.keysInRange(start, end);
    }

    // Rebuild index from disk.
    void rebuildIndex() { indexer_.rebuild(); }

    // Persist/load index.
    void saveIndex() const { indexer_.saveToFile(); }
    bool loadIndex() { return indexer_.loadFromFile(); }

    BlobStorage& storage() { return store_; }
    FastBlobIndexer& indexer() { return indexer_; }

private:
    BlobStorage store_;
    mutable FastBlobIndexer indexer_;
};

} // namespace blobstore
