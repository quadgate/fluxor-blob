// Micro-benchmark for I/O-bound blob storage operations.
// Measures: sequential put, sequential get, cached get, batch put, mmap read.

#include "blob_storage.hpp"
#include "blob_storage_io.hpp"

#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <random>
#include <cstdio>
#include <unistd.h>

using namespace blobstore;
using Clock = std::chrono::high_resolution_clock;

static std::string tmpdir() {
    return std::string("/tmp/blobstore_bench_") + std::to_string(::getpid());
}

static std::vector<unsigned char> randomData(std::size_t size) {
    std::vector<unsigned char> v(size);
    std::mt19937 rng(42);
    for (auto& b : v) b = static_cast<unsigned char>(rng() & 0xff);
    return v;
}

static void printRate(const char* label, std::size_t ops, std::size_t bytes, double secs) {
    double opsPerSec = ops / secs;
    double mbPerSec = (bytes / (1024.0 * 1024.0)) / secs;
    std::printf("%-24s %8zu ops  %8.2f ops/s  %8.2f MB/s  (%.3f s)\n",
                label, ops, opsPerSec, mbPerSec, secs);
}

int main() {
    const std::size_t numBlobs = 500;
    const std::size_t blobSize = 64 * 1024; // 64 KB each

    std::string root = tmpdir();
    BlobStorage store(root);
    store.init();

    // Prepare keys and data
    std::vector<std::string> keys;
    keys.reserve(numBlobs);
    for (std::size_t i = 0; i < numBlobs; ++i) {
        keys.push_back("key_" + std::to_string(i));
    }
    std::vector<unsigned char> data = randomData(blobSize);

    // 1. Sequential put
    {
        auto t0 = Clock::now();
        for (const auto& k : keys) {
            store.put(k, data);
        }
        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        printRate("Sequential put", numBlobs, numBlobs * blobSize, secs);
    }

    // 2. Sequential get (cold, no cache)
    {
        auto t0 = Clock::now();
        for (const auto& k : keys) {
            auto d = store.get(k);
            (void)d;
        }
        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        printRate("Sequential get (cold)", numBlobs, numBlobs * blobSize, secs);
    }

    // 3. Cached get (warm cache)
    {
        CachedBlobStorage cached(root, 128 * 1024 * 1024); // 128 MB cache
        // warm
        for (const auto& k : keys) {
            cached.get(k);
        }
        auto t0 = Clock::now();
        for (const auto& k : keys) {
            auto d = cached.get(k);
            (void)d;
        }
        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        printRate("Cached get (warm)", numBlobs, numBlobs * blobSize, secs);
    }

    // 4. Batch put (overwrites)
    {
        std::vector<std::pair<std::string, std::vector<unsigned char>>> items;
        items.reserve(numBlobs);
        for (const auto& k : keys) {
            items.emplace_back(k, data);
        }
        auto t0 = Clock::now();
        auto res = batchPut(store, items);
        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        printRate("Batch put", numBlobs, numBlobs * blobSize, secs);
    }

    // 5. Memory-mapped read
    {
        auto t0 = Clock::now();
        for (const auto& k : keys) {
            MappedBlob mb = MappedBlob::open(store, k);
            volatile unsigned char x = mb.data()[0]; // touch data
            (void)x;
        }
        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        printRate("mmap read", numBlobs, numBlobs * blobSize, secs);
    }

    std::cout << "\nBenchmark complete. Root: " << root << "\n";
    return 0;
}
