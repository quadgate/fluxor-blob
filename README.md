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

Notes
-----
- Keys are stored by hex-encoding; listing decodes back to the original key.
- Keys are case-sensitive.
