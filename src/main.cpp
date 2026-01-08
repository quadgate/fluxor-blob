#include "blob_storage.hpp"
#include <fcntl.h>      // open, O_RDONLY
#include <sys/mman.h>   // mmap, PROT_READ, MAP_PRIVATE, MAP_FAILED, madvise, MADV_SEQUENTIAL, munmap
#include <unistd.h>     // close, munmap
#include <algorithm>    // std::sort, std::max_element
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>

using namespace blobstore;

static void usage() {
    std::cerr << "Usage:\n"
                 "  blobstore init <root>\n"
                 "  blobstore push <root> <key> <file>\n"
                 "  blobstore get <root> <key> <out_file>\n"
                 "  blobstore exists <root> <key>\n"
                 "  blobstore list <root>\n"
                 "  blobstore rm <root> <key>\n"
                 "  blobstore stat <root> <key>\n";
}

static std::vector<unsigned char> readAll(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) throw std::runtime_error("Failed to open file: " + path);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];

    try {
        std::string bucket = "default";
        if (cmd == "init") {
            if (argc != 3) { usage(); return 1; }
            BlobStorage bs(argv[2]);
            bs.init(bucket);
            std::cout << "Initialized at " << bs.root() << "\n";
            return 0;
        } else if (cmd == "push") {
            if (argc != 5) { usage(); return 1; }
            BlobStorage bs(argv[2]);
            bs.init(bucket);
            std::string key = argv[3];
            std::string file = argv[4];
            auto data = readAll(file);
            bs.put(bucket, key, data);
            std::cout << "Stored key '" << key << "' size=" << data.size() << "\n";
            return 0;
        } else if (cmd == "get") {
            if (argc != 5) { usage(); return 1; }
            BlobStorage bs(argv[2]);
            std::string key = argv[3];
            std::string out = argv[4];
            bs.getToFile(bucket, key, out);
            std::cout << "Wrote to " << out << " size=" << bs.sizeOf(bucket, key) << "\n";
            return 0;
        } else if (cmd == "exists") {
            if (argc != 4) { usage(); return 1; }
            BlobStorage bs(argv[2]);
            std::string key = argv[3];
            std::cout << (bs.exists(bucket, key) ? "1" : "0") << "\n";
            return bs.exists(bucket, key) ? 0 : 2;
        } else if (cmd == "list") {
            if (argc != 3) { usage(); return 1; }
            BlobStorage bs(argv[2]);
            for (const auto& k : bs.list(bucket)) {
                std::cout << k << "\n";
            }
            return 0;
        } else if (cmd == "rm") {
            if (argc != 4) { usage(); return 1; }
            BlobStorage bs(argv[2]);
            std::string key = argv[3];
            bool ok = bs.remove(bucket, key);
            if (!ok) { std::cerr << "Not found: " << key << "\n"; return 2; }
            std::cout << "Removed '" << key << "'\n";
            return 0;
        } else if (cmd == "stat") {
            if (argc != 4) { usage(); return 1; }
            BlobStorage bs(argv[2]);
            std::string key = argv[3];
            if (!bs.exists(bucket, key)) { std::cerr << "Not found\n"; return 2; }
            std::cout << "size=" << bs.sizeOf(bucket, key) << "\n";
            return 0;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    usage();
    return 1;
}
