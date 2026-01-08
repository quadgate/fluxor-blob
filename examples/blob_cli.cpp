// blob_cli.cpp: Simple CLI for blob storage (put/get/list with folder-like keys)
// Usage:
//   ./blob_cli put <blob_root> <bucket> <key> <file>
//   ./blob_cli get <blob_root> <bucket> <key> <outfile>
//   ./blob_cli list <blob_root> <bucket> [prefix]
#include "blob_storage.hpp"
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <put|get|list> <blob_root> <bucket> ...\n";
        return 1;
    }
    std::string cmd = argv[1];
    std::string blob_root = argv[2];
    std::string bucket = argv[3];
    blobstore::BlobStorage store(blob_root);
    store.init(bucket);
    if (cmd == "put" && argc == 6) {
        std::string key = argv[4];
        std::string file = argv[5];
        std::ifstream ifs(file, std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        store.put(bucket, key, data);
        std::cout << "Put: " << key << " (" << data.size() << " bytes)\n";
    } else if (cmd == "get" && argc == 6) {
        std::string key = argv[4];
        std::string outfile = argv[5];
        auto data = store.get(bucket, key);
        std::ofstream ofs(outfile, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
        std::cout << "Get: " << key << " -> " << outfile << "\n";
    } else if (cmd == "list" && (argc == 4 || argc == 5)) {
        std::string prefix = (argc == 5) ? argv[4] : "";
        auto keys = store.list(bucket);
        for (const auto& k : keys) {
            if (prefix.empty() || k.find(prefix) == 0)
                std::cout << k << "\n";
        }
    } else {
        std::cerr << "Invalid command or arguments.\n";
        return 2;
    }
    return 0;
}
