# FastBlobIndexer and IndexedBlobStorage

This repository ships a simple file-backed `BlobStorage` plus an optional in-memory index for fast lookups, counts, and prefix/range queries. This document describes the design of the indexer components declared in `include/blob_indexer.hpp`.

Components
- `FastBlobIndexer`:
  - Maintains two synchronized structures:
    - `hashIndex_` (`unordered_map<string, BlobMeta>`): O(1) average key → metadata.
    - `sortedIndex_` (`map<string, BlobMeta*>`): ordered keys for prefix/range queries.
  - Thread-safety: guarded by a `mutex` for updates and reads.
  - Persistence: `saveToFile()` / `loadFromFile()` write/read a compact index file under the storage root (implementation keeps minimal coupling; path via `indexFilePath()`).
  - Rebuild: `rebuild()` scans on-disk layout (`BlobStorage::list()`, `sizeOf()`) and repopulates both structures.

- `IndexedBlobStorage`:
  - Wraps `BlobStorage` and a `FastBlobIndexer` to keep the index in sync.
  - `put()` and `remove()` update the index via `onPut()`/`onRemove()` after the underlying storage operation succeeds.
  - Exposes `exists()`, `getMeta()`, `list()`, `count()`, `totalBytes()`, and `keysWithPrefix()`/`keysInRange()` implemented against the index.
  - Lifecycle: call `init()` to ensure directories; optionally call `loadIndex()` (fast start) or `rebuildIndex()` (authoritative) before serving.

Disk Layout (recap)
- Root has `data/` with 256 shard subdirs based on first 2 hex digits of key-hex.
- Keys are hex-encoded (case-sensitive), safe for filesystem and deterministic.
- No external DB: the index can be reconstructed from files when needed.

Operational Guidance
- Startup: try `loadIndex()`; if it returns false (missing/corrupt), call `rebuildIndex()` and then `saveIndex()`.
- Mutations: after each `put()`/`remove()`, the index is updated in-memory; persist periodically or on shutdown.
- Prefix queries: `keysWithPrefix("users/")` uses the sorted map to binary-search the lower/upper bounds.
- Range queries: `keysInRange(start, end)` walks the ordered map from lower_bound(start) to lower_bound(end).

Complexity
- Point lookups: O(1) expected via hash.
- Prefix/range: O(log N + K) via `std::map` where K = number of returned keys.
- Rebuild: O(N log N) dominated by sorting inserts unless hints are used; typically I/O-bound from directory scans.

Trade-offs
- Dual-structure memory overhead vs. fast ordered traversals.
- Index persistence is optional; rebuild guarantees correctness if data on disk changes outside of the process.
- Coarse mutex is simple; fine-grained sharding is possible if higher write concurrency is required.

When to Use
- Many read operations, frequent prefix/range enumerations, or frequent existence checks where scanning the FS is too slow.
- For pure write-then-read bulk operations, you can use `BlobStorage` alone and build the index afterward.
