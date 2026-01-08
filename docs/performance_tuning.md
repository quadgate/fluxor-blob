# Performance Tuning Guide

This project is I/O-bound by design: throughput is frequently limited by filesystem latency and kernel page cache behavior. Below are practical tips to maximize performance for typical workloads.

Choosing an Access Path
- Small, hot keys: wrap `BlobStorage` with `CachedBlobStorage` and size the LRU to hold your working set.
- Large blobs (≥ 1 MB): use `MappedBlob` for zero-copy reads backed by the OS page cache.
- Bulk data movement: prefer `batchPut`/`batchGet` to amortize per-call overhead.
- Latency-sensitive: overlap I/O using `asyncPut`/`asyncGet`.

File System Layout
- Keep the storage root on a fast SSD/NVMe filesystem.
- The hex-sharded directory scheme (`data/aa/aaaa...`) avoids pathological fanout; no tuning required.

Kernel/Page Cache
- Reads benefit from the page cache; avoid frequent `drop_caches` in benchmarks.
- For sequential scans, `mmap`-based readers can hint with `MADV_SEQUENTIAL` and `MADV_WILLNEED`.

Build Flags
- Use `-O3 -DNDEBUG -march=native` for CLI/benchmarks to leverage CPU features (AVX2, etc.).
- Keep `-pthread` for async helpers and tests.

Benchmarking Tips
- Warm-up: run each test twice; the second run reflects cached reads.
- Pin: optionally use `taskset` to reduce variance from CPU migration.
- Isolate: ensure no other heavy I/O workloads on the same disk during measurement.

Error Handling
- Atomic writes (temp + rename) are used for durability. If your FS lacks rename guarantees across mountpoints, ensure the temp file lives on the same filesystem as the destination path.

Indexing
- Use `IndexedBlobStorage` when you need fast `exists`, global `count`, or prefix/range queries. Persist the index for fast startups and rebuild it if missing.
