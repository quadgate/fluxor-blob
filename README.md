Blob Storage (C++)
===================

A tiny, local, file-backed blob storage written in C++17.

Features
--------
- Put/get blobs by arbitrary string key
- Atomic writes (temp file + rename)
- Deterministic on-disk layout; no external deps or index files
- Simple CLI: put, get, exists, list, rm, stat

On-disk layout
--------------
- Root directory contains a `data/` folder.
- Keys are hex-encoded and sharded by first 2 hex digits to limit dir fanout:
  - Example for key "hello": `data/68/68656c6c6f`

Build
-----

Requirements: g++ (C++17) and make.

Commands:

```bash
make
```

This builds the CLI `bin/blobstore` and tests `bin/tests`.

Run
---

```bash
# Initialize storage root
./bin/blobstore init /tmp/bs

# Put a file under key "greeting"
echo "hello world" > /tmp/hello.txt
./bin/blobstore put /tmp/bs greeting /tmp/hello.txt

# Get it back
./bin/blobstore get /tmp/bs greeting /tmp/out.txt
cat /tmp/out.txt

# List keys
./bin/blobstore list /tmp/bs

# Exists / Stat
./bin/blobstore exists /tmp/bs greeting && echo exists
./bin/blobstore stat /tmp/bs greeting

# Remove
./bin/blobstore rm /tmp/bs greeting
```

Tests
-----

```bash
make test && ./bin/tests
```

Benchmark
---------

```bash
make bench && ./bin/bench
```

Outputs ops/s and MB/s for sequential put/get, cached reads, batch puts, and mmap reads.

Challenge Indexers
------------------

Looking for competitive-programming style indexers and micro-optimizations? See the challenge variants under [challenge/](challenge/README.md) with input generators and build/run instructions.

I/O-Bound Optimizations
-----------------------

This storage is **I/O-bound**: throughput is limited by disk seek and read/write latency rather than CPU.
Several techniques are available to mitigate this:

| Technique            | Header / Class           | Description |
|----------------------|--------------------------|-------------|
| **LRU read cache**   | `LRUCache`, `CachedBlobStorage` | Keeps recently-read blobs in memory; avoids repeated syscalls for hot keys. |
| **Memory-mapped I/O** | `MappedBlob`            | Zero-copy read via `mmap()`; lets the OS page cache handle caching and prefetch. |
| **Batch put/get**    | `batchPut`, `batchGet`   | Reduces per-call overhead when writing/reading many blobs at once. |
| **Async I/O**        | `asyncPut`, `asyncGet`   | Runs operations on background threads; caller can overlap I/O with compute. |

**Choosing a strategy**

- *Read-heavy, repeated keys*: Use `CachedBlobStorage` with an appropriately sized cache.
- *Large blobs (> 1 MB)*: Use `MappedBlob` to avoid copying into user-space.
- *Bulk imports/exports*: Use `batchPut` / `batchGet` for throughput.
- *Latency-sensitive apps*: Use `asyncPut` / `asyncGet` to avoid blocking the main thread.

Notes
-----
- Keys are stored by hex-encoding; listing decodes back to the original key.
- Keys are case-sensitive.

Further Reading
---------------
- Real-world usage: [docs/real_world_usage.md](docs/real_world_usage.md)
- Indexer design: [docs/indexer_design.md](docs/indexer_design.md)
- Performance tuning: [docs/performance_tuning.md](docs/performance_tuning.md)
- Versioning design: [docs/versioned_storage_design.md](docs/versioned_storage_design.md)
