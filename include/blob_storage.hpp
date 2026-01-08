#pragma once

#include <string>
#include <vector>

namespace blobstore {

class BlobStorage {
public:
    explicit BlobStorage(std::string root);

    // Ensures the storage directories exist for a bucket.
    void init(const std::string& bucket);


    // Versioned put: stores a new version (versionId can be a timestamp, UUID, etc.)
    void put(const std::string& bucket, const std::string& key, const std::vector<unsigned char>& data, const std::string& versionId = "");

    // Versioned get: retrieves a specific version or latest if versionId is empty
    std::vector<unsigned char> get(const std::string& bucket, const std::string& key, const std::string& versionId = "") const;

    // Versioned put from file
    void putFromFile(const std::string& bucket, const std::string& key, const std::string& path, const std::string& versionId = "");

    // Versioned get to file
    void getToFile(const std::string& bucket, const std::string& key, const std::string& path, const std::string& versionId = "") const;

    // Remove a specific version or all versions if versionId is empty
    bool remove(const std::string& bucket, const std::string& key, const std::string& versionId = "");

    // True if key (any version) exists
    bool exists(const std::string& bucket, const std::string& key) const;

    // List all keys in a bucket
    std::vector<std::string> list(const std::string& bucket) const;

    // List all versions for a key
    std::vector<std::string> listVersions(const std::string& bucket, const std::string& key) const;

    // Size in bytes of a specific version or latest if versionId is empty
    std::size_t sizeOf(const std::string& bucket, const std::string& key, const std::string& versionId = "") const;

    // Get the latest versionId for a key (empty if none)
    std::string getLatestVersionId(const std::string& bucket, const std::string& key) const;

    const std::string& root() const { return root_; }

private:
    std::string root_;

    static std::string hexEncode(const std::string& s);
    static std::string hexDecode(const std::string& hex);
    static std::string join(const std::string& a, const std::string& b);
    static bool ensureDir(const std::string& path);
    static bool fileExists(const std::string& path);
    static void writeFileAtomic(const std::string& path, const std::vector<unsigned char>& data);
    static std::vector<unsigned char> readFile(const std::string& path);
    static std::size_t fileSize(const std::string& path);

    // Returns full path for the key's blob file in a bucket.
    std::string pathForKey(const std::string& bucket, const std::string& key) const;
    // Returns data dir for a bucket.
    std::string dataDir(const std::string& bucket) const;
};

} // namespace blobstore
