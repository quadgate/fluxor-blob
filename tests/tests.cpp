#include "blob_storage.hpp"
#include "blob_storage_io.hpp"
#include "blob_indexer.hpp"

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
    std::string bucket = "default";
    BlobStorage bs(root);
    bs.init(bucket);

    std::string key = "greeting";
    std::vector<unsigned char> data = {'h','e','l','l','o'};
    bs.put(bucket, key, data);
    assert(bs.exists(bucket, key));
    assert(bs.sizeOf(bucket, key) == data.size());

    auto got = bs.get(bucket, key);
    assert(got == data);

    auto keys = bs.list(bucket);
    bool found = false;
    for (auto& k : keys) if (k == key) found = true;
    assert(found);

    bool removed = bs.remove(bucket, key);
    assert(removed);
    assert(!bs.exists(bucket, key));

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
    std::string bucket = "default";
    BlobStorage bs(root);
    bs.init(bucket);

    std::vector<std::pair<std::string, std::vector<unsigned char>>> items = {
        {"a", {'1'}},
        {"b", {'2'}},
        {"c", {'3'}},
    };

    auto results = batchPut(bs, items); // batchPut already patched to use default bucket
    assert(results.size() == 3);
    for (auto& r : results) {
        assert(r.success);
    }

    std::vector<std::string> keys = {"a", "b", "c", "missing"};
    auto got = batchGet(bs, keys); // batchGet already patched to use default bucket
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
    bs.init("default");

    std::vector<unsigned char> data = {'a','s','y','n','c'};

    auto futPut = asyncPut(bs, "asynckey", data); // asyncPut already patched to use default bucket
    futPut.get(); // wait

    auto futGet = asyncGet(bs, "asynckey"); // asyncGet already patched to use default bucket
    auto got = futGet.get();
    assert(got == data);

    std::cout << "  [PASS] testAsync\n";
}

// ---------------------------------------------------------------------------
// Test: MappedBlob (mmap read)
// ---------------------------------------------------------------------------
static void testMappedBlob() {
    std::string root = tmpdir("_mmap");
    std::string bucket = "default";
    BlobStorage bs(root);
    bs.init(bucket);

    std::vector<unsigned char> data = {'m','m','a','p','p','e','d'};
    bs.put(bucket, "mapkey", data);

    MappedBlob mb = MappedBlob::open(bs, "mapkey"); // MappedBlob::open only takes key
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
    std::string bucket = "default";
    BlobStorage bs(root);
    bs.init(bucket);

    // Empty blob
    bs.put(bucket, "empty", {});
    assert(bs.exists(bucket, "empty"));
    assert(bs.sizeOf(bucket, "empty") == 0);
    auto empty = bs.get(bucket, "empty");
    assert(empty.empty());

    // Key with special chars
    std::string specialKey = "foo/bar:baz?qux";
    std::vector<unsigned char> data = {'x'};
    bs.put(bucket, specialKey, data);
    assert(bs.exists(bucket, specialKey));
    assert(bs.get(bucket, specialKey) == data);

    // Overwrite
    bs.put(bucket, specialKey, {'y','z'});
    assert(bs.get(bucket, specialKey) == std::vector<unsigned char>({'y','z'}));

    // Double remove
    assert(bs.remove(bucket, specialKey));
    assert(!bs.remove(bucket, specialKey));

    std::cout << "  [PASS] testEdgeCases\n";
}

// ---------------------------------------------------------------------------
// Test: FastBlobIndexer
// ---------------------------------------------------------------------------
static void testFastBlobIndexer() {
    std::string root = tmpdir("_indexer");
    std::string bucket = "default";
    BlobStorage bs(root);
    bs.init(bucket);

    // Add some blobs.
    bs.put(bucket, "apple", {'a'});
    bs.put(bucket, "apricot", {'b'});
    bs.put(bucket, "banana", {'c'});
    bs.put(bucket, "cherry", {'d'});

    FastBlobIndexer indexer(bs);
    indexer.rebuild(bucket);

    // Count and exists.
    assert(indexer.count() == 4);
    assert(indexer.exists("apple"));
    assert(indexer.exists("banana"));
    assert(!indexer.exists("grape"));

    // getMeta.
    auto meta = indexer.getMeta("apple");
    assert(meta.has_value());
    assert(meta->size == 1);

    // allKeys (sorted).
    auto all = indexer.allKeys();
    assert(all.size() == 4);
    assert(all[0] == "apple");
    assert(all[1] == "apricot");
    assert(all[2] == "banana");
    assert(all[3] == "cherry");

    // Prefix query.
    auto ap = indexer.keysWithPrefix("ap");
    assert(ap.size() == 2);
    assert(ap[0] == "apple");
    assert(ap[1] == "apricot");

    // Range query.
    auto range = indexer.keysInRange("apricot", "cherry");
    assert(range.size() == 2); // apricot, banana
    assert(range[0] == "apricot");
    assert(range[1] == "banana");

    // onPut / onRemove.
    indexer.onPut("date", 5);
    assert(indexer.exists("date"));
    assert(indexer.count() == 5);

    indexer.onRemove("apple");
    assert(!indexer.exists("apple"));
    assert(indexer.count() == 4);

    // Save and load.
    indexer.saveToFile();
    indexer.clear();
    assert(indexer.count() == 0);

    bool loaded = indexer.loadFromFile();
    assert(loaded);
    assert(indexer.count() == 4);
    assert(indexer.exists("date"));
    assert(!indexer.exists("apple"));

    std::cout << "  [PASS] testFastBlobIndexer\n";
}

// ---------------------------------------------------------------------------
// Test: IndexedBlobStorage
// ---------------------------------------------------------------------------
static void testIndexedBlobStorage() {
    std::string root = tmpdir("_indexed");
    std::string bucket = "default";
    IndexedBlobStorage ibs(root, bucket);
    ibs.init();

    // Put blobs.
    ibs.put("users/alice", {'1'});
    ibs.put("users/bob", {'2'});
    ibs.put("logs/2026-01-08", {'3'});

    // Fast lookups.
    assert(ibs.count() == 3);
    assert(ibs.exists("users/alice"));
    assert(!ibs.exists("users/charlie"));

    auto meta = ibs.getMeta("users/bob");
    assert(meta.has_value());
    assert(meta->size == 1);

    // Prefix query.
    auto users = ibs.keysWithPrefix("users/");
    assert(users.size() == 2);

    // Get data.
    auto data = ibs.get("logs/2026-01-08");
    assert(data == std::vector<unsigned char>{'3'});

    // Remove.
    assert(ibs.remove("users/alice"));
    assert(ibs.count() == 2);
    assert(!ibs.exists("users/alice"));

    // Persist and reload.
    ibs.saveIndex();

    IndexedBlobStorage ibs2(root, bucket);
    ibs2.init(); // should load index
    assert(ibs2.count() == 2);
    assert(ibs2.exists("users/bob"));

    std::cout << "  [PASS] testIndexedBlobStorage\n";
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
    testFastBlobIndexer();
    testIndexedBlobStorage();

    std::cout << "All tests passed.\n";
    return 0;
}
