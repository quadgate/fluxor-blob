#include "blob_storage.hpp"
#include "blob_storage_io.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <unistd.h>
#include <future>

using namespace blobstore;

static std::string tmpdir(const char* suffix = "") {
    return std::string("/tmp/blobstore_test_") + std::to_string(::getpid()) + suffix;
}

// ---------------------------------------------------------------------------
// Test: Basic BlobStorage operations
// ---------------------------------------------------------------------------
static void testBasic() {
    std::string root = tmpdir("_basic");
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

    std::cout << "  [PASS] testBasic\n";
}

// ---------------------------------------------------------------------------
// Test: LRU Cache
// ---------------------------------------------------------------------------
static void testLRUCache() {
    LRUCache cache(1024); // 1 KB max

    auto d1 = std::make_shared<std::vector<unsigned char>>(100, 'a');
    auto d2 = std::make_shared<std::vector<unsigned char>>(100, 'b');

    cache.put("k1", d1);
    cache.put("k2", d2);

    assert(cache.get("k1") != nullptr);
    assert(cache.get("k2") != nullptr);
    assert(cache.get("k1")->at(0) == 'a');
    assert(cache.get("k2")->at(0) == 'b');

    // Invalidate
    cache.invalidate("k1");
    assert(cache.get("k1") == nullptr);

    // Eviction: add enough to exceed 1 KB
    for (int i = 0; i < 20; ++i) {
        cache.put("big_" + std::to_string(i), std::make_shared<std::vector<unsigned char>>(100, 'x'));
    }
    // k2 should have been evicted
    assert(cache.get("k2") == nullptr);

    cache.clear();
    assert(cache.currentBytes() == 0);

    std::cout << "  [PASS] testLRUCache\n";
}

// ---------------------------------------------------------------------------
// Test: CachedBlobStorage
// ---------------------------------------------------------------------------
static void testCachedBlobStorage() {
    std::string root = tmpdir("_cached");
    CachedBlobStorage cbs(root, 10 * 1024); // 10 KB cache
    cbs.init();

    std::vector<unsigned char> data = {'c','a','c','h','e','d'};
    cbs.put("mykey", data);

    // First get populates cache
    auto got1 = cbs.get("mykey");
    assert(got1 == data);

    // Second get should hit cache
    auto got2 = cbs.get("mykey");
    assert(got2 == data);

    // Update invalidates cache
    std::vector<unsigned char> data2 = {'n','e','w'};
    cbs.put("mykey", data2);
    auto got3 = cbs.get("mykey");
    assert(got3 == data2);

    // Remove invalidates cache
    cbs.remove("mykey");
    assert(!cbs.exists("mykey"));

    std::cout << "  [PASS] testCachedBlobStorage\n";
}

// ---------------------------------------------------------------------------
// Test: Batch put/get
// ---------------------------------------------------------------------------
static void testBatch() {
    std::string root = tmpdir("_batch");
    BlobStorage bs(root);
    bs.init();

    std::vector<std::pair<std::string, std::vector<unsigned char>>> items = {
        {"a", {'1'}},
        {"b", {'2'}},
        {"c", {'3'}},
    };

    auto results = batchPut(bs, items);
    assert(results.size() == 3);
    for (auto& r : results) {
        assert(r.success);
    }

    std::vector<std::string> keys = {"a", "b", "c", "missing"};
    auto got = batchGet(bs, keys);
    assert(got.size() == 4);
    assert(got[0].second == std::vector<unsigned char>{'1'});
    assert(got[1].second == std::vector<unsigned char>{'2'});
    assert(got[2].second == std::vector<unsigned char>{'3'});
    assert(got[3].second.empty()); // missing key

    std::cout << "  [PASS] testBatch\n";
}

// ---------------------------------------------------------------------------
// Test: Async put/get
// ---------------------------------------------------------------------------
static void testAsync() {
    std::string root = tmpdir("_async");
    BlobStorage bs(root);
    bs.init();

    std::vector<unsigned char> data = {'a','s','y','n','c'};

    auto futPut = asyncPut(bs, "asynckey", data);
    futPut.get(); // wait

    auto futGet = asyncGet(bs, "asynckey");
    auto got = futGet.get();
    assert(got == data);

    std::cout << "  [PASS] testAsync\n";
}

// ---------------------------------------------------------------------------
// Test: MappedBlob (mmap read)
// ---------------------------------------------------------------------------
static void testMappedBlob() {
    std::string root = tmpdir("_mmap");
    BlobStorage bs(root);
    bs.init();

    std::vector<unsigned char> data = {'m','m','a','p','p','e','d'};
    bs.put("mapkey", data);

    MappedBlob mb = MappedBlob::open(bs, "mapkey");
    assert(mb.valid());
    assert(mb.size() == data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        assert(mb.data()[i] == data[i]);
    }

    // Move semantics
    MappedBlob mb2 = std::move(mb);
    assert(mb2.valid());
    assert(!mb.valid());

    std::cout << "  [PASS] testMappedBlob\n";
}

// ---------------------------------------------------------------------------
// Test: Edge cases
// ---------------------------------------------------------------------------
static void testEdgeCases() {
    std::string root = tmpdir("_edge");
    BlobStorage bs(root);
    bs.init();

    // Empty blob
    bs.put("empty", {});
    assert(bs.exists("empty"));
    assert(bs.sizeOf("empty") == 0);
    auto empty = bs.get("empty");
    assert(empty.empty());

    // Key with special chars
    std::string specialKey = "foo/bar:baz?qux";
    std::vector<unsigned char> data = {'x'};
    bs.put(specialKey, data);
    assert(bs.exists(specialKey));
    assert(bs.get(specialKey) == data);

    // Overwrite
    bs.put(specialKey, {'y','z'});
    assert(bs.get(specialKey) == std::vector<unsigned char>({'y','z'}));

    // Double remove
    assert(bs.remove(specialKey));
    assert(!bs.remove(specialKey));

    std::cout << "  [PASS] testEdgeCases\n";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "Running tests...\n";

    testBasic();
    testLRUCache();
    testCachedBlobStorage();
    testBatch();
    testAsync();
    testMappedBlob();
    testEdgeCases();

    std::cout << "All tests passed.\n";
    return 0;
}
