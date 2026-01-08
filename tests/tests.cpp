#include "blob_storage.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <unistd.h>

using namespace blobstore;

static std::string tmpdir() {
    const char* base = "/tmp";
    std::string path = std::string(base) + "/blobstore_test_" + std::to_string(::getpid());
    return path;
}

int main() {
    std::string root = tmpdir();
    BlobStorage bs(root);
    bs.init();

    std::string key = "greeting";
    std::vector<unsigned char> data = {'h','e','l','l','o'};
    bs.put(key, data);
    assert(bs.exists(key));
    assert(bs.sizeOf(key) == data.size());

    auto got = bs.get(key);
    assert(got == data);

    auto keys = bs.list();
    bool found = false;
    for (auto& k : keys) if (k == key) found = true;
    assert(found);

    bool removed = bs.remove(key);
    assert(removed);
    assert(!bs.exists(key));

    std::cout << "All tests passed.\n";
    return 0;
}
