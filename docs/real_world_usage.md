# Real-World Usage Guide

This guide shows practical patterns for using BlobStorage in production applications.

## Use Case 1: Media CDN Backend

Store and serve user-uploaded images/videos with caching.

```cpp
#include "blob_storage_io.hpp"
#include <string>
#include <iostream>

class MediaStorage {
public:
    MediaStorage(const std::string& root, size_t cacheMB = 512)
        : storage_(root, cacheMB * 1024 * 1024) {
        storage_.init();
    }

    // Upload with content-based key
    std::string upload(const std::vector<unsigned char>& data, 
                       const std::string& userId, 
                       const std::string& ext) {
        // Generate key: user_id/timestamp_hash.ext
        auto timestamp = std::time(nullptr);
        auto hash = std::hash<std::string>{}(
            std::string(data.begin(), data.end())
        );
        
        std::string key = userId + "/" + 
                         std::to_string(timestamp) + "_" +
                         std::to_string(hash) + "." + ext;
        
        storage_.put(key, data);
        return key;
    }

    // Serve with automatic cache
    std::vector<unsigned char> serve(const std::string& key) {
        return storage_.get(key);  // Cached automatically
    }

    // List user's media
    std::vector<std::string> listUserMedia(const std::string& userId) {
        auto all = storage_.list();
        std::vector<std::string> result;
        std::string prefix = userId + "/";
        
        for (const auto& k : all) {
            if (k.substr(0, prefix.length()) == prefix) {
                result.push_back(k);
            }
        }
        return result;
    }

private:
    blobstore::CachedBlobStorage storage_;
};

// Usage
int main() {
    MediaStorage media("/var/data/media", 1024);  // 1GB cache
    
    // Upload
    std::vector<unsigned char> image = readFile("photo.jpg");
    std::string url = media.upload(image, "user123", "jpg");
    std::cout << "Uploaded: " << url << "\n";
    
    // Serve (fast on repeated access)
    auto data = media.serve(url);
    writeFile("cached.jpg", data);
    
    return 0;
}
```

## Use Case 2: Document Store with Indexing

Store JSON documents with fast prefix queries.

```cpp
#include "blob_indexer.hpp"
#include <nlohmann/json.hpp>  // or your JSON lib

class DocumentStore {
public:
    DocumentStore(const std::string& root) : store_(root) {
        store_.init();
        if (!store_.loadIndex()) {
            store_.rebuildIndex();
        }
    }

    // Store document
    void putDoc(const std::string& collection,
                const std::string& docId,
                const nlohmann::json& doc) {
        std::string key = collection + "/" + docId;
        std::string serialized = doc.dump();
        std::vector<unsigned char> data(serialized.begin(), 
                                       serialized.end());
        store_.put(key, data);
    }

    // Get document
    nlohmann::json getDoc(const std::string& collection,
                          const std::string& docId) {
        std::string key = collection + "/" + docId;
        auto data = store_.get(key);
        std::string serialized(data.begin(), data.end());
        return nlohmann::json::parse(serialized);
    }

    // List all docs in collection (fast prefix query)
    std::vector<std::string> listCollection(const std::string& collection) {
        return store_.keysWithPrefix(collection + "/");
    }

    // Count docs in collection
    size_t countCollection(const std::string& collection) {
        return listCollection(collection).size();
    }

    // Persist index for fast restart
    ~DocumentStore() {
        store_.saveIndex();
    }

private:
    blobstore::IndexedBlobStorage store_;
};

// Usage
int main() {
    DocumentStore db("/var/data/docs");
    
    // Store users
    db.putDoc("users", "alice", {{"name", "Alice"}, {"age", 30}});
    db.putDoc("users", "bob", {{"name", "Bob"}, {"age", 25}});
    db.putDoc("orders", "ord123", {{"user", "alice"}, {"total", 99.99}});
    
    // Query
    auto users = db.listCollection("users");
    std::cout << "Users: " << users.size() << "\n";
    
    auto alice = db.getDoc("users", "alice");
    std::cout << "Name: " << alice["name"] << "\n";
    
    return 0;
}
```

## Use Case 3: Log Aggregation

Collect and query application logs efficiently.

```cpp
#include "blob_storage_io.hpp"
#include <sstream>
#include <chrono>

class LogStore {
public:
    LogStore(const std::string& root) : store_(root) {
        store_.init();
    }

    // Append log entry
    void log(const std::string& service,
             const std::string& level,
             const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()
        ).count();
        
        // Key: service/date/level_timestamp
        time_t t = ts;
        struct tm* tm_info = std::gmtime(&t);
        char date[16];
        strftime(date, sizeof(date), "%Y%m%d", tm_info);
        
        std::string key = service + "/" + date + "/" + 
                         level + "_" + std::to_string(ts);
        
        std::string entry = std::to_string(ts) + " [" + level + "] " + 
                           service + ": " + message + "\n";
        std::vector<unsigned char> data(entry.begin(), entry.end());
        
        store_.put(key, data);
    }

    // Read logs for a day (batch get)
    std::vector<std::string> getServiceLogs(const std::string& service,
                                            const std::string& date) {
        auto all = store_.list();
        std::vector<std::string> keys;
        std::string prefix = service + "/" + date + "/";
        
        for (const auto& k : all) {
            if (k.substr(0, prefix.length()) == prefix) {
                keys.push_back(k);
            }
        }
        
        // Batch read
        auto results = blobstore::batchGet(store_.storage(), keys);
        
        std::vector<std::string> logs;
        for (const auto& [key, data] : results) {
            logs.emplace_back(data.begin(), data.end());
        }
        return logs;
    }

private:
    blobstore::CachedBlobStorage store_;
};

// Usage
int main() {
    LogStore logs("/var/data/logs");
    
    // Write logs
    logs.log("web-server", "INFO", "Server started");
    logs.log("web-server", "ERROR", "Connection failed");
    logs.log("worker", "INFO", "Job completed");
    
    // Query logs
    auto today = logs.getServiceLogs("web-server", "20260108");
    for (const auto& entry : today) {
        std::cout << entry;
    }
    
    return 0;
}
```

## Use Case 4: Backup System

Incremental backups with deduplication.

```cpp
#include "blob_storage.hpp"
#include <openssl/sha.h>  // or your hash lib

class BackupStore {
public:
    BackupStore(const std::string& root) : store_(root) {
        store_.init();
    }

    // Backup file with content-addressed storage
    std::string backup(const std::string& filepath) {
        auto data = readFile(filepath);
        
        // Hash content for deduplication
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(data.data(), data.size(), hash);
        
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') 
               << (int)hash[i];
        }
        std::string contentHash = ss.str();
        
        // Key: hash (automatic dedup)
        if (!store_.exists(contentHash)) {
            store_.put(contentHash, data);
        }
        
        return contentHash;
    }

    // Restore file
    void restore(const std::string& contentHash, 
                 const std::string& outpath) {
        store_.getToFile(contentHash, outpath);
    }

    // Batch backup with async I/O
    std::vector<std::string> backupBatch(
        const std::vector<std::string>& files
    ) {
        std::vector<std::future<std::string>> futures;
        
        for (const auto& f : files) {
            futures.push_back(std::async(std::launch::async, 
                [this, f]() { return this->backup(f); }
            ));
        }
        
        std::vector<std::string> hashes;
        for (auto& fut : futures) {
            hashes.push_back(fut.get());
        }
        return hashes;
    }

private:
    blobstore::BlobStorage store_;
    
    std::vector<unsigned char> readFile(const std::string& path) {
        std::ifstream ifs(path, std::ios::binary);
        return std::vector<unsigned char>(
            std::istreambuf_iterator<char>(ifs),
            std::istreambuf_iterator<char>()
        );
    }
};

// Usage
int main() {
    BackupStore backup("/mnt/backups");
    
    // Backup files
    auto hash1 = backup.backup("/home/user/document.pdf");
    auto hash2 = backup.backup("/home/user/photo.jpg");
    
    std::cout << "Backed up: " << hash1 << "\n";
    
    // Restore
    backup.restore(hash1, "/tmp/restored.pdf");
    
    return 0;
}
```

## Use Case 5: ML Model Registry

Store and version machine learning models.

```cpp
#include "blob_storage_io.hpp"
#include <map>

class ModelRegistry {
public:
    ModelRegistry(const std::string& root) : store_(root) {
        store_.init();
    }

    // Register model with metadata
    void registerModel(const std::string& modelName,
                      const std::string& version,
                      const std::vector<unsigned char>& weights,
                      const std::map<std::string, std::string>& metadata) {
        // Store weights
        std::string weightsKey = modelName + "/" + version + "/weights";
        store_.put(weightsKey, weights);
        
        // Store metadata as JSON
        std::string metaKey = modelName + "/" + version + "/metadata";
        std::string metaJson = serializeMetadata(metadata);
        std::vector<unsigned char> metaData(metaJson.begin(), 
                                           metaJson.end());
        store_.put(metaKey, metaData);
    }

    // Load model (with mmap for large files)
    blobstore::MappedBlob loadModel(const std::string& modelName,
                                    const std::string& version) {
        std::string key = modelName + "/" + version + "/weights";
        return blobstore::MappedBlob::open(store_.storage(), key);
    }

    // List model versions
    std::vector<std::string> listVersions(const std::string& modelName) {
        auto all = store_.list();
        std::set<std::string> versions;
        std::string prefix = modelName + "/";
        
        for (const auto& k : all) {
            if (k.substr(0, prefix.length()) == prefix) {
                auto parts = split(k, '/');
                if (parts.size() >= 2) {
                    versions.insert(parts[1]);
                }
            }
        }
        return std::vector<std::string>(versions.begin(), versions.end());
    }

private:
    blobstore::CachedBlobStorage store_;
    
    std::string serializeMetadata(
        const std::map<std::string, std::string>& meta
    ) {
        std::stringstream ss;
        ss << "{";
        bool first = true;
        for (const auto& [k, v] : meta) {
            if (!first) ss << ",";
            ss << "\"" << k << "\":\"" << v << "\"";
            first = false;
        }
        ss << "}";
        return ss.str();
    }
    
    std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> result;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) {
            result.push_back(item);
        }
        return result;
    }
};

// Usage
int main() {
    ModelRegistry registry("/var/data/models");
    
    // Register model
    auto weights = loadWeights("model.bin");
    registry.registerModel("sentiment", "v1.0", weights, {
        {"accuracy", "0.95"},
        {"framework", "pytorch"},
        {"date", "2026-01-08"}
    });
    
    // Load for inference (zero-copy via mmap)
    auto model = registry.loadModel("sentiment", "v1.0");
    // Use model.data() and model.size() for inference
    
    return 0;
}
```

## Deployment Patterns

### Single-Node Setup
```bash
# Production directory structure
/var/data/blobstore/
  data/           # Sharded blob storage
  index/          # Optional index files
  tmp/            # Temp files for atomic writes

# Systemd service
[Unit]
Description=Blob Storage Service
After=network.target

[Service]
Type=simple
User=blobstore
ExecStart=/usr/local/bin/blobstore-server --root=/var/data/blobstore
Restart=always

[Install]
WantedBy=multi-user.target
```

### Multi-Node with Sync
```bash
# Use rsync for replication
rsync -avz --delete /var/data/blobstore/ backup-node:/var/data/blobstore/

# Or use filesystem-level replication (ZFS send/receive, DRBD, etc.)
```

### Docker Deployment
```dockerfile
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y g++ make
COPY . /app
WORKDIR /app
RUN make

VOLUME /data
EXPOSE 8080

CMD ["./bin/blobstore-server", "--root=/data"]
```

## Performance Tips

1. **For hot reads:** Use `CachedBlobStorage` with appropriate cache size
2. **For large files:** Use `MappedBlob` for zero-copy access
3. **For bulk ops:** Use `batchPut`/`batchGet` to amortize overhead
4. **For async:** Use `asyncPut`/`asyncGet` to overlap I/O with compute
5. **For queries:** Use `IndexedBlobStorage` with persisted index

## Monitoring

```cpp
// Add metrics
class MonitoredStorage {
    blobstore::BlobStorage store_;
    std::atomic<uint64_t> putCount_{0};
    std::atomic<uint64_t> getCount_{0};
    
public:
    void put(const std::string& key, 
             const std::vector<unsigned char>& data) {
        auto start = std::chrono::steady_clock::now();
        store_.put(key, data);
        auto dur = std::chrono::steady_clock::now() - start;
        
        putCount_++;
        // Report to monitoring system (Prometheus, etc.)
    }
};
```
