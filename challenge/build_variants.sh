#!/bin/bash
# Build all challenge variants
# Usage: ./challenge/build_variants.sh

set -e

echo "=== Building Fast Blob Indexer Variants ==="
mkdir -p bin

CXXFLAGS="-O3 -DNDEBUG -std=c++17 -pipe -march=native"

echo "Building baseline..."
g++ $CXXFLAGS challenge/fast_blob_indexer.cpp -o bin/fbi_basic

echo "Building ultra (mmap + arena)..."
g++ $CXXFLAGS challenge/fast_blob_indexer_ultra.cpp -o bin/fbi_ultra

echo "Building ultimate (AVX2 + prefetch)..."
if g++ $CXXFLAGS -mavx2 challenge/fast_blob_indexer_ultimate.cpp -o bin/fbi_ultimate 2>/dev/null; then
    echo "  ✓ Built with AVX2"
else
    echo "  ✗ AVX2 not supported, skipping"
fi

echo "Building hyper (parallel queries)..."
if g++ $CXXFLAGS -mavx2 challenge/fast_blob_indexer_hyper.cpp -o bin/fbi_hyper -pthread 2>/dev/null; then
    echo "  ✓ Built with AVX2"
else
    echo "  ✗ AVX2 not supported, skipping"
fi

echo "Building extreme (io_uring)..."
if g++ $CXXFLAGS challenge/fast_blob_indexer_extreme.cpp -o bin/fbi_extreme -luring 2>/dev/null; then
    echo "  ✓ Built with io_uring"
else
    echo "  ✗ liburing not available, skipping"
fi

echo "Building uring variant..."
if g++ $CXXFLAGS challenge/fast_blob_indexer_uring.cpp -o bin/fbi_uring -luring 2>/dev/null; then
    echo "  ✓ Built with io_uring"
else
    echo "  ✗ liburing not available, skipping"
fi

echo "Building godlike (AVX2 + huge pages + io_uring)..."
if g++ $CXXFLAGS -mavx2 challenge/fast_blob_indexer_godlike.cpp -o bin/fbi_godlike -luring 2>/dev/null; then
    echo "  ✓ Built with AVX2 + io_uring"
else
    echo "  ✗ Dependencies not met, skipping"
fi

echo ""
echo "=== Built Variants ==="
ls -lh bin/fbi_* 2>/dev/null || echo "No variants built"
