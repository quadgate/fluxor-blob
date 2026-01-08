# Fast Blob Indexer Challenge

This folder contains competitive-programming-style implementations of a super-fast in-memory indexer optimized for large-scale key lookups. Each program reads from stdin and writes to stdout in a strict text format (described below). They are standalone executables independent from the library in src/ and headers in include/.

Goals
- Handle up to N ≈ 1,000,000 blobs and Q ≈ 100,000 queries.
- Latency-focused parsing and hash-lookup; favor cache locality and zero-copy when possible.
- Explore multiple optimization tiers: simple, ultra, ultimate, hyper, extreme, uring, godlike.

Input/Output Format
- First line: N (number of blob metadata lines)
- Next N lines: key size offset
  - key: arbitrary ASCII (no spaces), length up to ~64 in provided variants
  - size: unsigned 64-bit integer
  - offset: unsigned 64-bit integer
- Next line: Q (number of queries)
- Next Q lines: key

For each query key, output either:
- "size offset" if key is present
- "NOTFOUND" otherwise

Generators
- Go: challenge/gen.go (random, reproducible; 50% hits by default)
  - Example: `go run challenge/gen.go 1000000 100000 16 > input.txt`
- Python: challenge/gen_test.py (variable key lengths; ~70% hits)
  - Example: `python3 challenge/gen_test.py 1000000 100000 > input.txt`

Build Quickstart
Use a modern g++ with C++17+. AVX2-enabled variants benefit from `-march=native` or `-mavx2`.

Examples (produce binaries under bin/):

```bash
mkdir -p bin

# Baseline (unordered_map + fast I/O)
g++ -O3 -DNDEBUG -std=c++17 -pipe -march=native \
  challenge/fast_blob_indexer.cpp -o bin/fbi_basic

# Ultra (arena, custom open-addressing hashmap, mmap stdin)
g++ -O3 -DNDEBUG -std=c++17 -pipe -march=native \
  challenge/fast_blob_indexer_ultra.cpp -o bin/fbi_ultra

# Ultimate (SIMD compare, prefetch, double-buffered output)
g++ -O3 -DNDEBUG -std=c++17 -pipe -march=native -mavx2 \
  challenge/fast_blob_indexer_ultimate.cpp -o bin/fbi_ultimate

# Hyper (parallel queries)
g++ -O3 -DNDEBUG -std=c++17 -pipe -march=native -mavx2 \
  challenge/fast_blob_indexer_hyper.cpp -o bin/fbi_hyper -pthread

# Extreme (io_uring output, object pool)
g++ -O3 -DNDEBUG -std=c++17 -pipe -march=native \
  challenge/fast_blob_indexer_extreme.cpp -o bin/fbi_extreme -luring

# io_uring-focused variant
g++ -O3 -DNDEBUG -std=c++17 -pipe -march=native \
  challenge/fast_blob_indexer_uring.cpp -o bin/fbi_uring -luring

# Godlike (all the tricks: AVX2 + huge pages + io_uring)
g++ -O3 -DNDEBUG -std=c++17 -pipe -march=native -mavx2 \
  challenge/fast_blob_indexer_godlike.cpp -o bin/fbi_godlike -luring
```

Run

```bash
# Generate input (1M entries, 100k queries)
go run challenge/gen.go 1000000 100000 16 > /tmp/input.txt

# Run a variant
time bin/fbi_ultra < /tmp/input.txt > /tmp/output.txt

# Verify correctness against baseline
time bin/fbi_basic < /tmp/input.txt > /tmp/output_basic.txt
diff -u /tmp/output_basic.txt /tmp/output.txt && echo OK
```

Variant Summary
- fast_blob_indexer.cpp: Baseline using `std::unordered_map` and fast ASCII I/O.
- fast_blob_indexer_ultra.cpp: `mmap` input, arena allocator, custom open-addressing map.
- fast_blob_indexer_ultimate.cpp: SIMD memcmp (AVX2), prefetch, double-buffered output.
- fast_blob_indexer_hyper.cpp: Parallel query processing with pre-hashed queries and prefetch.
- fast_blob_indexer_extreme.cpp: io_uring double-buffered writer, object pool.
- fast_blob_indexer_uring.cpp: io_uring output focus with custom hashmap.
- fast_blob_indexer_godlike.cpp: Combines AVX2 compare, huge pages, io_uring, prefetch.

Performance Tips
- Compile with `-O3 -DNDEBUG -march=native` for maximum gains on local CPU.
- Use `taskset`/`numactl` to pin threads or memory on NUMA systems.
- Prefer feeding via regular files (not pipes) so `mmap` paths are used.
- For `io_uring` variants, ensure `liburing` and kernel support are available.
- On cloud VMs without AVX2, remove `-mavx2` from compile commands.

Troubleshooting
- If `mmap` complains about stdin, redirect input from a file: `... < input.txt`.
- If `-luring` link fails, install liburing dev package (Ubuntu: `sudo apt-get install -y liburing-dev`).
- If AVX2 illegal instruction occurs, rebuild without `-mavx2`.
