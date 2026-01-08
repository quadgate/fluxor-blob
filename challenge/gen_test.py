#!/usr/bin/env python3
"""Generate test data for Fast Blob Indexer benchmark."""

import random
import string
import sys

def random_key(length=20):
    return ''.join(random.choices(string.ascii_lowercase + string.digits + '_', k=length))

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1_000_000
    q = int(sys.argv[2]) if len(sys.argv) > 2 else 100_000

    # Generate N blobs
    keys = []
    print(n)
    offset = 0
    for _ in range(n):
        key = random_key(random.randint(10, 50))
        size = random.randint(1, 100_000_000)
        keys.append(key)
        print(f"{key} {size} {offset}")
        offset += size

    # Generate Q queries (70% hits, 30% misses)
    print(q)
    for _ in range(q):
        if random.random() < 0.7 and keys:
            print(random.choice(keys))
        else:
            print(random_key(25))

if __name__ == "__main__":
    main()
