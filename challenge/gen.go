// gen.go - Fast test input generator for Fast Blob Indexer
// Usage: go run gen.go [n] [q] [keylen] > input.txt
// Default: n=1000000, q=100000, keylen=16

package main

import (
	"bufio"
	"fmt"
	"math/rand"
	"os"
	"strconv"
)

func main() {
	n := 1000000
	q := 100000
	keyLen := 16

	if len(os.Args) > 1 {
		n, _ = strconv.Atoi(os.Args[1])
	}
	if len(os.Args) > 2 {
		q, _ = strconv.Atoi(os.Args[2])
	}
	if len(os.Args) > 3 {
		keyLen, _ = strconv.Atoi(os.Args[3])
	}

	rng := rand.New(rand.NewSource(42))
	w := bufio.NewWriterSize(os.Stdout, 1<<20) // 1MB buffer
	defer w.Flush()

	// Generate random key
	key := make([]byte, keyLen)
	genKey := func() string {
		for i := range key {
			key[i] = byte('a' + rng.Intn(26))
		}
		return string(key)
	}

	// Store keys for queries (some will match)
	keys := make([]string, n)

	// Print N
	fmt.Fprintln(w, n)

	// Generate blobs
	for i := 0; i < n; i++ {
		k := genKey()
		keys[i] = k
		sz := rng.Intn(10000)
		off := rng.Intn(1000000)
		fmt.Fprintf(w, "%s %d %d\n", k, sz, off)
	}

	// Print Q
	fmt.Fprintln(w, q)

	// Generate queries (50% existing keys, 50% random)
	for i := 0; i < q; i++ {
		if rng.Intn(2) == 0 && n > 0 {
			// Existing key
			fmt.Fprintln(w, keys[rng.Intn(n)])
		} else {
			// Random key (likely not found)
			fmt.Fprintln(w, genKey())
		}
	}
}
