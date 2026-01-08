#include "blob_storage.hpp"

#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

namespace blobstore {


std::string BlobStorage::dataDir(const std::string& bucket) const {
    return join(join(root_, bucket), "data");
}

BlobStorage::BlobStorage(std::string root) : root_(std::move(root)) {}

void BlobStorage::init(const std::string& bucket) {
    if (!ensureDir(root_)) {
        throw std::runtime_error("Failed to create root: " + root_);
    }
    std::string d = dataDir(bucket);
    if (!ensureDir(d)) {
        throw std::runtime_error("Failed to create data dir: " + d);
    }
}

std::string BlobStorage::hexEncode(const std::string& s) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : s) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    return oss.str();
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

std::string BlobStorage::hexDecode(const std::string& hex) {
    if (hex.size() % 2 != 0) throw std::runtime_error("Invalid hex string");
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexVal(hex[i]);
        int lo = hexVal(hex[i+1]);
        if (hi < 0 || lo < 0) throw std::runtime_error("Invalid hex string");
        unsigned char b = static_cast<unsigned char>((hi << 4) | lo);
        out.push_back(static_cast<char>(b));
    }
    return out;
}

std::string BlobStorage::join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

bool BlobStorage::ensureDir(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
    if (errno == ENOENT) {
        // create parent first
        auto pos = path.find_last_of('/');
        if (pos != std::string::npos) {
            if (!ensureDir(path.substr(0, pos))) return false;
            return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
        }
    }
    return errno == EEXIST; // ok if already exists
}

bool BlobStorage::fileExists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void BlobStorage::writeFileAtomic(const std::string& path, const std::vector<unsigned char>& data) {
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        ensureDir(path.substr(0, pos));
    }
    std::string tmp = path + ".tmp-" + std::to_string(::getpid());
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs) throw std::runtime_error("Failed to open temp file: " + tmp);
        ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!ofs) throw std::runtime_error("Failed to write temp file: " + tmp);
        ofs.flush();
        ofs.close();
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        // cleanup tmp on failure
        ::unlink(tmp.c_str());
        throw std::runtime_error("Failed to rename temp file: " + std::string(std::strerror(errno)));
    }
}

std::vector<unsigned char> BlobStorage::readFile(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) throw std::runtime_error("Failed to stat file: " + path);
    size_t sz = st.st_size;
    if (sz == 0) return {};
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("Failed to open file: " + path);
    void* data = mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        ::close(fd);
        throw std::runtime_error("mmap failed: " + path);
    }
    madvise(data, sz, MADV_SEQUENTIAL);
    std::vector<unsigned char> buf(sz);
    std::memcpy(buf.data(), data, sz);
    munmap(data, sz);
    ::close(fd);
    return buf;
}

std::size_t BlobStorage::fileSize(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) throw std::runtime_error("Failed to stat file: " + path);
    return static_cast<std::size_t>(st.st_size);
}

std::string BlobStorage::pathForKey(const std::string& bucket, const std::string& key, const std::string& versionId) const {
    std::string hex = hexEncode(key);
    std::string shard = hex.size() >= 2 ? hex.substr(0, 2) : "zz";
    std::string base = join(join(dataDir(bucket), shard), hex);
    if (!versionId.empty())
        return base + "__" + versionId;
    return base;
}

void BlobStorage::put(const std::string& bucket, const std::string& key, const std::vector<unsigned char>& data, const std::string& versionId) {
    std::string p = pathForKey(bucket, key, versionId);
    writeFileAtomic(p, data);

    // --- Policy: Keep only 3 latest versions ---
    std::vector<std::string> versions = listVersions(bucket, key);
    // Sort descending (latest first)
    std::sort(versions.rbegin(), versions.rend());
    for (size_t i = 3; i < versions.size(); ++i) {
        std::string oldPath = pathForKey(bucket, key, versions[i]);
        if (fileExists(oldPath)) {
            ::unlink(oldPath.c_str());
        }
    }
}

std::vector<unsigned char> BlobStorage::get(const std::string& bucket, const std::string& key, const std::string& versionId) const {
    std::string p = pathForKey(bucket, key, versionId.empty() ? getLatestVersionId(bucket, key) : versionId);
    if (!fileExists(p)) throw std::runtime_error("Key not found: " + key);
    return readFile(p);
}

void BlobStorage::putFromFile(const std::string& bucket, const std::string& key, const std::string& path, const std::string& versionId) {
    auto data = readFile(path);
    put(bucket, key, data, versionId);
}

void BlobStorage::getToFile(const std::string& bucket, const std::string& key, const std::string& path, const std::string& versionId) const {
    auto data = get(bucket, key, versionId);
    writeFileAtomic(path, data);
}

bool BlobStorage::remove(const std::string& bucket, const std::string& key, const std::string& versionId) {
    if (!versionId.empty()) {
        std::string p = pathForKey(bucket, key, versionId);
        if (!fileExists(p)) return false;
        return ::unlink(p.c_str()) == 0;
    } else {
        // Remove all versions
        bool any = false;
        for (const auto& v : listVersions(bucket, key)) {
            std::string p = pathForKey(bucket, key, v);
            if (fileExists(p)) {
                ::unlink(p.c_str());
                any = true;
            }
        }
        return any;
    }
}

bool BlobStorage::exists(const std::string& bucket, const std::string& key) const {
    return !listVersions(bucket, key).empty();
}

std::vector<std::string> BlobStorage::list(const std::string& bucket) const {
    std::vector<std::string> keys;
    std::string base = dataDir(bucket);
    DIR* d = ::opendir(base.c_str());
    if (!d) return keys; // treat as empty
    struct dirent* ent;
    while ((ent = ::readdir(d)) != nullptr) {
        std::string shard = ent->d_name;
        if (shard == "." || shard == "..") continue;
        std::string shardPath = join(base, shard);
        DIR* d2 = ::opendir(shardPath.c_str());
        if (!d2) continue;
        struct dirent* ent2;
        while ((ent2 = ::readdir(d2)) != nullptr) {
            std::string hex = ent2->d_name;
            if (hex == "." || hex == "..") continue;
            try {
                std::string key = hexDecode(hex);
                // Only add base key (not __version)
                if (key.find("__") == std::string::npos)
                    keys.push_back(key);
            } catch (...) {
                // skip invalid filenames
            }
        }
        ::closedir(d2);
    }
    ::closedir(d);
    return keys;
}
std::vector<std::string> BlobStorage::listVersions(const std::string& bucket, const std::string& key) const {
    std::vector<std::string> versions;
    std::string hex = hexEncode(key);
    std::string shard = hex.size() >= 2 ? hex.substr(0, 2) : "zz";
    std::string base = join(join(dataDir(bucket), shard), "");
    DIR* d = ::opendir(base.c_str());
    if (!d) return versions;
    struct dirent* ent;
    std::string prefix = hex + "__";
    while ((ent = ::readdir(d)) != nullptr) {
        std::string fname = ent->d_name;
        if (fname == "." || fname == "..") continue;
        if (fname == hex) versions.push_back(""); // unversioned
        else if (fname.find(prefix) == 0) {
            versions.push_back(fname.substr(prefix.size()));
        }
    }
    ::closedir(d);
    return versions;
}

std::size_t BlobStorage::sizeOf(const std::string& bucket, const std::string& key, const std::string& versionId) const {
    return fileSize(pathForKey(bucket, key, versionId.empty() ? getLatestVersionId(bucket, key) : versionId));
}
std::string BlobStorage::getLatestVersionId(const std::string& bucket, const std::string& key) const {
    // List all versions and return the lexicographically last (latest)
    auto versions = listVersions(bucket, key);
    if (versions.empty()) return "";
    return *std::max_element(versions.begin(), versions.end());
}

} // namespace blobstore
