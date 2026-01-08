# Versioned Blob Storage (Single-File per Key)

This document proposes a durable, append-only versioning design that stores all versions of a key inside a single data file, plus a compact sidecar index. It extends the existing deterministic sharded layout without introducing external dependencies.

Goals
- Maintain multiple versions per key with efficient append and random read.
- Keep a single data file per key to minimize inode overhead and maximize locality.
- Crash-safe writes using temp files + fsync + rename for index updates.
- Simple recovery without background compaction required for correctness.
- Backward compatible with current directory sharding and hex-encoded keys.

Non-Goals (v1)
- Cross-key transactions.
- Transparent de-duplication or compression.
- Multi-writer concurrency on the same key.

On-Disk Layout
- For key `K` (hex-encoded as `H` under shard `SS`):
  - Data file: `data/SS/H.dat` (append-only, raw blob payloads concatenated)
  - Index file: `data/SS/H.idx` (fixed-size entries + optional trailer)

Index Entry (fixed 32 bytes)
- `[u64 offset][u64 size][u64 unix_ts][u64 version]`
  - `offset`: byte offset in `.dat` where this version’s payload begins
  - `size`: payload length in bytes
  - `unix_ts`: seconds since epoch when written (for auditing/retention)
  - `version`: monotonically increasing version number (starts at 1)

Optional Trailer (8 bytes)
- Final 8 bytes store `u64 count` = number of entries; allows O(1) index size read.
- If missing (e.g., older format), derive count via file length / 32.

Durability and Atomicity
- Data append:
  - Open `.dat` with `O_WRONLY|O_CREAT|O_APPEND` and write payload.
  - Call `fsync(dat_fd)` (or `fdatasync`) after payload write to persist data.
- Index update (atomic publish):
  - Read current `.idx` (if exists) to get `lastVersion` and `count`.
  - Append the new entry to a temp file `.idx.tmp` built as:
    - Copy existing `.idx` contents (or memory buffer) + new 32-byte entry
    - Write optional trailer with updated count
    - `fsync(tmp_fd)`
  - `rename(".idx.tmp", ".idx")` to atomically publish the new index.
  - Optionally `fsync(dir)` for directory entry durability on crashy filesystems.

Crash Recovery Rules
- If `.dat` has extra bytes beyond the last index entry’s `[offset,size]` range (power loss after data fsync but before index rename), those bytes are invisible until a future index entry references them; they remain safe.
- If `.idx` is partially written (temp), atomic rename prevents readers from seeing it.
- On startup, `.idx` defines the authoritative set of visible versions; `.dat` is treated as append-only backing store.

Concurrency Model
- Single-writer per key (process-level or advisory lock) to guarantee version monotonicity and avoid index races.
- Multiple readers allowed concurrently; readers only open `.idx` for read and `pread()` from `.dat`.
- Optional advisory lock file `H.lock` (not required if higher layer ensures single writer).

API Surface (new wrapper)
- `class VersionedBlobStorage` (wraps existing `BlobStorage` root and sharding):
  - `void init();`
  - `uint64_t put(const std::string& key, const std::vector<unsigned char>& data);`
    - Appends a new version; returns the assigned version number.
  - `std::vector<unsigned char> get(const std::string& key, std::optional<uint64_t> version = {});`
    - Reads specific version or latest if `version` is empty.
  - `bool getToFile(const std::string& key, const std::string& path, std::optional<uint64_t> version = {});`
  - `std::vector<uint64_t> listVersions(const std::string& key);`
  - `std::optional<uint64_t> head(const std::string& key);` (latest version)
  - `bool removeVersion(const std::string& key, uint64_t version);` (logical delete; see below)
  - `void compact(const std::string& key);` (optional GC/compaction)

Semantics
- Version numbering is per-key and strictly increasing by 1 from 1.
- `removeVersion()` (v1) is a logical deletion that writes a tombstone entry to the index (size=0, offset=prev_offset, ts=now, version=next). Physical space is reclaimed only during `compact()`.
- `head()` skips tombstones to return the latest non-deleted version.

Fast Paths
- Latest version read:
  - Read last index entry (from trailer or last 32 bytes block) and follow to `.dat`.
- Random version read:
  - Compute entry offset as `32 * (version-1)`; `pread()` that entry; validate `version` matches.
- List versions:
  - If trailer present, read count; otherwise derive by `filesize / 32` and generate `[1..count]`, optionally filtering tombstones.

Operations
- Put (append):
  - Compute data offset = `filesize(.dat)` (or via `lseek(SEEK_END)` when not using `O_APPEND`).
  - Write payload to `.dat`, `fsync`.
  - Build new index (`old_entries + new_entry [+ trailer]`) into `.idx.tmp`, `fsync`, then `rename` to `.idx`.
  - Return `prev_version + 1`.
- Get:
  - Resolve version (or `head`), read corresponding index entry, `pread()` payload from `.dat`.
- Remove (tombstone):
  - Append a zero-size entry with new version number to `.idx` using the same atomic publish flow.
- Compact (optional):
  - Rewrite `.dat.new` and `.idx.new` excluding tombstoned versions; rename over originals.

Index Caching (optional)
- Maintain a small in-memory cache of parsed index entries per hot key to avoid repeated file I/O.
- Cache invalidation on successful `put()`/`removeVersion()`.

Interaction with Existing Indexer
- `IndexedBlobStorage` can be extended to understand the `.idx` file for `sizeOf(key)` (latest) and to list keys. Version-aware queries (e.g., prefix on keys and version filters) can be added later.

Performance Considerations
- Append-only `.dat` supports high write throughput and good locality for sequential readers.
- `.idx` grows by 32 bytes per version; reading latest requires O(1) disk I/O (single `pread`).
- For extremely high version counts, map `.idx` with `mmap` to accelerate binary searches or scans.

Compatibility & Migration
- Existing one-blob-per-key stores can be migrated by creating `.dat` with the current payload at offset 0 and writing a single index entry with `version=1`.
- Mixed stores (some keys versioned, some not) are allowed; non-versioned keys continue to live at `data/SS/H` (the old path). `VersionedBlobStorage` only operates on `.dat/.idx` keys.

Security & Integrity (optional)
- Add checksum per entry (e.g., 32-bit CRC) in a future extension to detect corruption.
- Consider fs-verity or per-blob MAC if tamper resistance is required.

Open Questions
- Do we require `fsync(dir)` after renames for all filesystems? Default: optional with a tunable.
- Should `removeVersion()` be hard delete (physical space reclaimed immediately)? Default: tombstone for simplicity and speed.
- Maximum key length assumptions for stack buffers in helpers (align with current codebase conventions).

Implementation Notes (v1)
- Place implementation under `src/versioned_blob_storage.cpp` and public header at `include/versioned_blob_storage.hpp`.
- Reuse existing sharding and hex-encoding utilities from `BlobStorage`.
- Add unit tests for append, latest read, random version read, tombstone handling, crash-like scenarios (simulated by skipping rename), and compaction.
