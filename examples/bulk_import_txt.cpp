// bulk_import_txt.cpp: Import all .txt files in a directory into BlobStorage bucket
// Usage: ./bulk_import_txt <txt_dir> <blob_root> <bucket>
#include "blob_storage.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <txt_dir> <blob_root> <bucket>\n";
        return 1;
    }
    std::string txt_dir = argv[1];
    std::string blob_root = argv[2];
    std::string bucket = argv[3];
    blobstore::BlobStorage store(blob_root);
    store.init(bucket);
    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(txt_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".txt") continue;
        std::ifstream ifs(entry.path(), std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        std::string key = entry.path().filename().string();
        store.put(bucket, key, data);
        if (++count % 1000 == 0) std::cout << "Imported: " << count << " files\n";
    }
    std::cout << "Done. Imported " << count << " .txt files into bucket '" << bucket << "'\n";
    return 0;
}
