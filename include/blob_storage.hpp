#pragma once

#include <string>
#include <vector>

namespace blobstore {

class BlobStorage {
public:
    explicit BlobStorage(std::string root);

    // Ensures the storage directories exist.
    void init();

    // Stores the bytes at key. Overwrites if exists.
    void put(const std::string& key, const std::vector<unsigned char>& data);

    // Reads the bytes for key. Throws std::runtime_error if not found.
    std::vector<unsigned char> get(const std::string& key) const;

    // Writes file contents to key.
    void putFromFile(const std::string& key, const std::string& path);

    // Writes blob to file path; overwrites path if exists.
    void getToFile(const std::string& key, const std::string& path) const;

    // Removes key; returns true if removed, false if not found.
    bool remove(const std::string& key);

    // True if key exists.
    bool exists(const std::string& key) const;

    // Returns list of all keys; may be slow for large stores.
    std::vector<std::string> list() const;

    // Size in bytes of stored blob; throws if not found.
    std::size_t sizeOf(const std::string& key) const;

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

    // Returns full path for the key's blob file.
    std::string pathForKey(const std::string& key) const;
};

} // namespace blobstore
