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

static std::string dataDir(const std::string& root) {
    return root + "/data";
}

BlobStorage::BlobStorage(std::string root) : root_(std::move(root)) {}

void BlobStorage::init() {
    if (!ensureDir(root_)) {
        throw std::runtime_error("Failed to create root: " + root_);
    }
    std::string d = dataDir(root_);
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
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) throw std::runtime_error("Failed to open file: " + path);
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return buf;
}

std::size_t BlobStorage::fileSize(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) throw std::runtime_error("Failed to stat file: " + path);
    return static_cast<std::size_t>(st.st_size);
}

std::string BlobStorage::pathForKey(const std::string& key) const {
    std::string hex = hexEncode(key);
    std::string shard = hex.size() >= 2 ? hex.substr(0, 2) : "zz";
    return join(join(dataDir(root_), shard), hex);
}

void BlobStorage::put(const std::string& key, const std::vector<unsigned char>& data) {
    std::string p = pathForKey(key);
    writeFileAtomic(p, data);
}

std::vector<unsigned char> BlobStorage::get(const std::string& key) const {
    std::string p = pathForKey(key);
    if (!fileExists(p)) throw std::runtime_error("Key not found: " + key);
    return readFile(p);
}

void BlobStorage::putFromFile(const std::string& key, const std::string& path) {
    auto data = readFile(path);
    put(key, data);
}

void BlobStorage::getToFile(const std::string& key, const std::string& path) const {
    auto data = get(key);
    writeFileAtomic(path, data);
}

bool BlobStorage::remove(const std::string& key) {
    std::string p = pathForKey(key);
    if (!fileExists(p)) return false;
    return ::unlink(p.c_str()) == 0;
}

bool BlobStorage::exists(const std::string& key) const {
    return fileExists(pathForKey(key));
}

std::vector<std::string> BlobStorage::list() const {
    std::vector<std::string> keys;
    std::string base = dataDir(root_);
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

std::size_t BlobStorage::sizeOf(const std::string& key) const {
    return fileSize(pathForKey(key));
}

} // namespace blobstore
