// GODLIKE Blob Indexer - Maximum performance
// - SIMD memcmp (AVX2 256-bit)
// - Huge pages (2MB TLB, transparent or explicit)
// - io_uring async I/O
// - Lock-free object pool
// - Prefetch + batch processing
//
// Target: < 0.15s for N=10^6, Q=10^5

#define _GNU_SOURCE
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>
#include <thread>
#include <atomic>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <liburing.h>
#include <immintrin.h>

// ---------------------------------------------------------------------------
// SIMD memcmp using AVX2 (256-bit = 32 bytes at a time)
// ---------------------------------------------------------------------------

#ifdef __AVX2__

inline bool simd_memeq(const char* a, const char* b, std::size_t len) {
    // Handle small lengths without SIMD
    if (len < 32) {
        return std::memcmp(a, b, len) == 0;
    }

    // Process 32 bytes at a time
    while (len >= 32) {
        __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));
        __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b));
        __m256i cmp = _mm256_cmpeq_epi8(va, vb);
        int mask = _mm256_movemask_epi8(cmp);
        if (mask != -1) return false; // -1 = 0xFFFFFFFF = all equal
        a += 32;
        b += 32;
        len -= 32;
    }

    // Handle remaining bytes (16 bytes with SSE)
    if (len >= 16) {
        __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
        __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
        __m128i cmp = _mm_cmpeq_epi8(va, vb);
        int mask = _mm_movemask_epi8(cmp);
        if (mask != 0xFFFF) return false;
        a += 16;
        b += 16;
        len -= 16;
    }

    // Handle remaining bytes
    while (len > 0) {
        if (*a++ != *b++) return false;
        --len;
    }

    return true;
}

#else

inline bool simd_memeq(const char* a, const char* b, std::size_t len) {
    return std::memcmp(a, b, len) == 0;
}

#endif

// ---------------------------------------------------------------------------
// Huge pages allocator
// ---------------------------------------------------------------------------

inline void* alloc_huge(std::size_t size) {
    // Try transparent huge pages first via mmap
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                     -1, 0);
    
    if (ptr == MAP_FAILED) {
        // Fallback: regular mmap with MADV_HUGEPAGE hint
        ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
        if (ptr != MAP_FAILED) {
            madvise(ptr, size, MADV_HUGEPAGE);
        }
    }
    
    return (ptr == MAP_FAILED) ? nullptr : ptr;
}

inline void free_huge(void* ptr, std::size_t size) {
    if (ptr) munmap(ptr, size);
}

// ---------------------------------------------------------------------------
// Lock-free Object Pool
// ---------------------------------------------------------------------------

template <typename T, std::size_t PoolSize = 4096>
class ObjectPool {
public:
    ObjectPool() : head_(0), tail_(0) {
        for (std::size_t i = 0; i < PoolSize; ++i) {
            pool_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    ~ObjectPool() {
        for (std::size_t i = 0; i < PoolSize; ++i) {
            T* obj = pool_[i].load(std::memory_order_relaxed);
            if (obj) delete obj;
        }
    }

    T* get() {
        std::size_t head = head_.load(std::memory_order_relaxed);
        while (head != tail_.load(std::memory_order_acquire)) {
            if (head_.compare_exchange_weak(head, (head + 1) % PoolSize,
                                            std::memory_order_acq_rel)) {
                T* obj = pool_[head].exchange(nullptr, std::memory_order_acquire);
                if (obj) { obj->reset(); return obj; }
            }
            head = head_.load(std::memory_order_relaxed);
        }
        return new T();
    }

    void put(T* obj) {
        if (!obj) return;
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        std::size_t next = (tail + 1) % PoolSize;
        if (next == head_.load(std::memory_order_acquire)) { delete obj; return; }
        T* expected = nullptr;
        if (pool_[tail].compare_exchange_strong(expected, obj, std::memory_order_release)) {
            tail_.compare_exchange_strong(tail, next, std::memory_order_release);
        } else {
            delete obj;
        }
    }

private:
    std::atomic<T*> pool_[PoolSize];
    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;
};

// ---------------------------------------------------------------------------
// Buffer for pool
// ---------------------------------------------------------------------------

struct Buffer {
    static constexpr std::size_t CAPACITY = 128 * 1024;
    char data[CAPACITY];
    std::size_t size = 0;
    void reset() { size = 0; }
};

ObjectPool<Buffer> g_bufferPool;

// ---------------------------------------------------------------------------
// FNV-1a Hash (unrolled)
// ---------------------------------------------------------------------------

inline std::size_t fnv1a(const char* data, std::size_t len) noexcept {
    constexpr std::size_t FNV_OFFSET = 14695981039346656037ULL;
    constexpr std::size_t FNV_PRIME = 1099511628211ULL;
    std::size_t hash = FNV_OFFSET;
    
    while (len >= 8) {
        hash ^= static_cast<unsigned char>(data[0]); hash *= FNV_PRIME;
        hash ^= static_cast<unsigned char>(data[1]); hash *= FNV_PRIME;
        hash ^= static_cast<unsigned char>(data[2]); hash *= FNV_PRIME;
        hash ^= static_cast<unsigned char>(data[3]); hash *= FNV_PRIME;
        hash ^= static_cast<unsigned char>(data[4]); hash *= FNV_PRIME;
        hash ^= static_cast<unsigned char>(data[5]); hash *= FNV_PRIME;
        hash ^= static_cast<unsigned char>(data[6]); hash *= FNV_PRIME;
        hash ^= static_cast<unsigned char>(data[7]); hash *= FNV_PRIME;
        data += 8; len -= 8;
    }
    while (len > 0) {
        hash ^= static_cast<unsigned char>(*data++);
        hash *= FNV_PRIME;
        --len;
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Huge-page Arena Allocator
// ---------------------------------------------------------------------------

class HugeArena {
public:
    explicit HugeArena(std::size_t capacity) : capacity_(capacity), offset_(0) {
        // Round up to 2MB boundary for huge pages
        std::size_t aligned = (capacity + (2 * 1024 * 1024 - 1)) & ~(2 * 1024 * 1024 - 1);
        data_ = static_cast<char*>(alloc_huge(aligned));
        if (!data_) {
            // Fallback to regular allocation
            data_ = static_cast<char*>(std::aligned_alloc(64, capacity));
        }
        allocSize_ = aligned;
    }

    ~HugeArena() {
        if (allocSize_ >= 2 * 1024 * 1024) {
            free_huge(data_, allocSize_);
        } else {
            std::free(data_);
        }
    }

    char* alloc(const char* src, std::size_t len) {
        std::size_t off = offset_.fetch_add(len + 1, std::memory_order_relaxed);
        if (off + len + 1 > capacity_) return nullptr;
        char* dst = data_ + off;
        std::memcpy(dst, src, len);
        dst[len] = '\0';
        return dst;
    }

private:
    char* data_;
    std::size_t capacity_;
    std::size_t allocSize_;
    std::atomic<std::size_t> offset_;
};

// ---------------------------------------------------------------------------
// Hash Map with SIMD compare
// ---------------------------------------------------------------------------

struct alignas(32) BlobEntry {
    const char* key;
    uint32_t keyLen;
    uint32_t hash32; // Store lower 32 bits of hash for quick check
    uint64_t size;
    uint64_t offset;
};

class FastHashMap {
public:
    explicit FastHashMap(std::size_t capacity) {
        capacity_ = 1;
        while (capacity_ < capacity * 2) capacity_ <<= 1;
        mask_ = capacity_ - 1;
        
        // Allocate with huge pages
        std::size_t byteSize = capacity_ * sizeof(BlobEntry);
        entries_ = static_cast<BlobEntry*>(alloc_huge(byteSize));
        if (!entries_) {
            entries_ = static_cast<BlobEntry*>(std::aligned_alloc(64, byteSize));
        }
        hugeSize_ = byteSize;
        
        for (std::size_t i = 0; i < capacity_; ++i) {
            entries_[i].key = nullptr;
        }
    }

    ~FastHashMap() {
        if (hugeSize_ >= 2 * 1024 * 1024) {
            free_huge(entries_, hugeSize_);
        } else {
            std::free(entries_);
        }
    }

    void insert(const char* key, uint32_t keyLen, uint64_t size, uint64_t offset) {
        std::size_t h = fnv1a(key, keyLen);
        uint32_t h32 = static_cast<uint32_t>(h);
        std::size_t idx = h & mask_;
        while (entries_[idx].key != nullptr) {
            idx = (idx + 1) & mask_;
        }
        entries_[idx] = {key, keyLen, h32, size, offset};
    }

    void prefetch(const char* key, std::size_t keyLen) const {
        std::size_t h = fnv1a(key, keyLen) & mask_;
        __builtin_prefetch(&entries_[h], 0, 3);
    }

    const BlobEntry* find(const char* key, std::size_t keyLen) const {
        std::size_t h = fnv1a(key, keyLen);
        uint32_t h32 = static_cast<uint32_t>(h);
        std::size_t idx = h & mask_;
        
        while (entries_[idx].key != nullptr) {
            // Quick hash check first
            if (entries_[idx].hash32 == h32 &&
                entries_[idx].keyLen == keyLen &&
                simd_memeq(entries_[idx].key, key, keyLen)) {
                return &entries_[idx];
            }
            idx = (idx + 1) & mask_;
        }
        return nullptr;
    }

private:
    BlobEntry* entries_;
    std::size_t capacity_;
    std::size_t mask_;
    std::size_t hugeSize_;
};

// ---------------------------------------------------------------------------
// io_uring writer
// ---------------------------------------------------------------------------

class IOUringWriter {
public:
    static constexpr int QUEUE_DEPTH = 64;

    IOUringWriter(int fd) : fd_(fd), pending_(0), pos_(0) {
        fallback_ = (io_uring_queue_init(QUEUE_DEPTH, &ring_, 0) < 0);
        buf_ = g_bufferPool.get();
    }

    ~IOUringWriter() {
        flush(); drain();
        if (!fallback_) io_uring_queue_exit(&ring_);
        if (buf_) g_bufferPool.put(buf_);
    }

    void write(const char* data, std::size_t len) {
        if (pos_ + len > Buffer::CAPACITY) submitBuffer();
        std::memcpy(buf_->data + pos_, data, len);
        pos_ += len;
    }

    void writeU64(uint64_t x) {
        char tmp[24];
        int len = 0;
        if (x == 0) { write("0", 1); return; }
        while (x > 0) { tmp[len++] = '0' + (x % 10); x /= 10; }
        for (int i = 0; i < len / 2; ++i) {
            char t = tmp[i]; tmp[i] = tmp[len-1-i]; tmp[len-1-i] = t;
        }
        write(tmp, len);
    }

    void writeChar(char c) { write(&c, 1); }
    void writeStr(const char* s) { write(s, std::strlen(s)); }
    void flush() { if (pos_ > 0) submitBuffer(); }

private:
    int fd_;
    io_uring ring_;
    bool fallback_;
    Buffer* buf_;
    std::size_t pos_;
    int pending_;

    void submitBuffer() {
        if (pos_ == 0) return;
        if (fallback_) { ::write(fd_, buf_->data, pos_); pos_ = 0; return; }
        while (pending_ >= QUEUE_DEPTH - 1) drainOne();
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_write(sqe, fd_, buf_->data, pos_, -1);
        io_uring_submit(&ring_);
        pending_++;
        Buffer* newBuf = g_bufferPool.get();
        g_bufferPool.put(buf_);
        buf_ = newBuf;
        pos_ = 0;
    }

    void drainOne() {
        struct io_uring_cqe* cqe;
        if (io_uring_wait_cqe(&ring_, &cqe) == 0) {
            io_uring_cqe_seen(&ring_, cqe);
            pending_--;
        }
    }

    void drain() { while (pending_ > 0) drainOne(); }
};

// ---------------------------------------------------------------------------
// Fast parsing
// ---------------------------------------------------------------------------

inline uint64_t parseU64(const char*& p) {
    uint64_t x = 0;
    while (*p >= '0' && *p <= '9') { x = x * 10 + (*p - '0'); ++p; }
    return x;
}

inline void skipSpaces(const char*& p) { while (*p == ' ' || *p == '\t') ++p; }
inline void skipLine(const char*& p) { while (*p && *p != '\n') ++p; if (*p == '\n') ++p; }

struct ParsedBlob { const char* keyStart; uint32_t keyLen; uint64_t size; uint64_t offset; };
struct Query { const char* keyStart; uint32_t keyLen; };

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    struct stat st;
    if (fstat(STDIN_FILENO, &st) != 0 || st.st_size == 0) {
        fprintf(stderr, "Error: stdin must be a file for mmap\n");
        return 1;
    }

    std::size_t fileSize = static_cast<std::size_t>(st.st_size);
    
    // mmap with MAP_POPULATE + huge pages hint
    char* data = static_cast<char*>(mmap(nullptr, fileSize, PROT_READ,
                                          MAP_PRIVATE | MAP_POPULATE, STDIN_FILENO, 0));
    if (data == MAP_FAILED) { perror("mmap"); return 1; }
    
    madvise(data, fileSize, MADV_SEQUENTIAL | MADV_WILLNEED | MADV_HUGEPAGE);

    const char* p = data;
    uint64_t n = parseU64(p);
    skipLine(p);

    // Huge-page arena
    HugeArena arena(n * 40 + 2 * 1024 * 1024);
    std::vector<ParsedBlob> blobs(n);

    // Parse all blobs
    for (uint64_t i = 0; i < n; ++i) {
        const char* keyStart = p;
        while (*p > ' ') ++p;
        uint32_t keyLen = static_cast<uint32_t>(p - keyStart);
        skipSpaces(p);
        uint64_t sz = parseU64(p);
        skipSpaces(p);
        uint64_t off = parseU64(p);
        skipLine(p);
        blobs[i] = {keyStart, keyLen, sz, off};
    }

    // Parallel arena allocation
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    if (numThreads > 8) numThreads = 8;

    std::vector<std::thread> threads;
    std::atomic<uint64_t> idx{0};
    const uint64_t chunkSize = 16384;

    for (unsigned t = 0; t < numThreads; ++t) {
        threads.emplace_back([&]() {
            while (true) {
                uint64_t start = idx.fetch_add(chunkSize, std::memory_order_relaxed);
                if (start >= n) break;
                uint64_t stop = std::min(start + chunkSize, n);
                for (uint64_t i = start; i < stop; ++i) {
                    char* key = arena.alloc(blobs[i].keyStart, blobs[i].keyLen);
                    blobs[i].keyStart = key;
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    // Build hash map (with huge pages)
    FastHashMap hashMap(n);
    for (uint64_t i = 0; i < n; ++i) {
        hashMap.insert(blobs[i].keyStart, blobs[i].keyLen, blobs[i].size, blobs[i].offset);
    }

    // Parse Q
    uint64_t q = parseU64(p);
    skipLine(p);

    // Parse all queries
    std::vector<Query> queries(q);
    const char* end = data + fileSize;
    for (uint64_t i = 0; i < q; ++i) {
        const char* keyStart = p;
        while (p < end && *p > ' ') ++p;
        queries[i] = {keyStart, static_cast<uint32_t>(p - keyStart)};
        skipLine(p);
    }

    // Process queries with prefetching (batch of 16)
    IOUringWriter out(STDOUT_FILENO);
    constexpr std::size_t BATCH = 16;

    for (uint64_t i = 0; i < q; i += BATCH) {
        uint64_t batchEnd = std::min(i + BATCH, q);

        // Prefetch all in batch
        for (uint64_t j = i; j < batchEnd; ++j) {
            hashMap.prefetch(queries[j].keyStart, queries[j].keyLen);
        }

        // Process batch
        for (uint64_t j = i; j < batchEnd; ++j) {
            const BlobEntry* e = hashMap.find(queries[j].keyStart, queries[j].keyLen);
            if (e) {
                out.writeU64(e->size);
                out.writeChar(' ');
                out.writeU64(e->offset);
                out.writeChar('\n');
            } else {
                out.writeStr("NOTFOUND\n");
            }
        }
    }

    out.flush();
    munmap(data, fileSize);
    return 0;
}
