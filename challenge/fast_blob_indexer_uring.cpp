// Extreme Blob Indexer - io_uring + Object Pool
// - io_uring for async vectored I/O
// - Lock-free object pool (sync.Pool style)
// - All previous optimizations
//
// Target: < 0.2s for N=10^6, Q=10^5

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

// ---------------------------------------------------------------------------
// Lock-free Object Pool (similar to Go's sync.Pool)
// ---------------------------------------------------------------------------

template <typename T, std::size_t PoolSize = 4096>
class ObjectPool {
public:
    ObjectPool() : head_(0), tail_(0) {
        // Pre-allocate pool slots
        for (std::size_t i = 0; i < PoolSize; ++i) {
            pool_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    ~ObjectPool() {
        // Clean up any remaining objects
        for (std::size_t i = 0; i < PoolSize; ++i) {
            T* obj = pool_[i].load(std::memory_order_relaxed);
            if (obj) delete obj;
        }
    }

    // Get object from pool, or create new one
    T* get() {
        // Try to pop from pool (lock-free)
        std::size_t head = head_.load(std::memory_order_relaxed);
        while (head != tail_.load(std::memory_order_acquire)) {
            if (head_.compare_exchange_weak(head, (head + 1) % PoolSize,
                                            std::memory_order_acq_rel)) {
                T* obj = pool_[head].exchange(nullptr, std::memory_order_acquire);
                if (obj) {
                    obj->reset(); // Reset for reuse
                    return obj;
                }
            }
            head = head_.load(std::memory_order_relaxed);
        }
        // Pool empty, allocate new
        return new T();
    }

    // Return object to pool
    void put(T* obj) {
        if (!obj) return;

        std::size_t tail = tail_.load(std::memory_order_relaxed);
        std::size_t next = (tail + 1) % PoolSize;

        // If pool is full, just delete
        if (next == head_.load(std::memory_order_acquire)) {
            delete obj;
            return;
        }

        // Try to push to pool
        T* expected = nullptr;
        if (pool_[tail].compare_exchange_strong(expected, obj,
                                                 std::memory_order_release)) {
            tail_.compare_exchange_strong(tail, next, std::memory_order_release);
        } else {
            delete obj; // Race condition, discard
        }
    }

private:
    std::atomic<T*> pool_[PoolSize];
    alignas(64) std::atomic<std::size_t> head_; // Cache line separation
    alignas(64) std::atomic<std::size_t> tail_;
};

// ---------------------------------------------------------------------------
// Reusable buffer (for pool)
// ---------------------------------------------------------------------------

struct Buffer {
    static constexpr std::size_t CAPACITY = 64 * 1024; // 64KB
    char data[CAPACITY];
    std::size_t size = 0;

    void reset() { size = 0; }
};

// Global buffer pool
ObjectPool<Buffer> g_bufferPool;

// ---------------------------------------------------------------------------
// FNV-1a Hash
// ---------------------------------------------------------------------------

struct FNV1aHash {
    std::size_t operator()(std::string_view sv) const noexcept {
        constexpr std::size_t FNV_OFFSET = 14695981039346656037ULL;
        constexpr std::size_t FNV_PRIME = 1099511628211ULL;
        std::size_t hash = FNV_OFFSET;
        for (char c : sv) {
            hash ^= static_cast<std::size_t>(static_cast<unsigned char>(c));
            hash *= FNV_PRIME;
        }
        return hash;
    }
};

// ---------------------------------------------------------------------------
// Arena Allocator (thread-safe, lock-free)
// ---------------------------------------------------------------------------

class Arena {
public:
    explicit Arena(std::size_t capacity)
        : data_(static_cast<char*>(std::aligned_alloc(64, capacity))),
          capacity_(capacity), offset_(0) {}

    ~Arena() { std::free(data_); }

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
    std::atomic<std::size_t> offset_;
};

// ---------------------------------------------------------------------------
// Custom Hash Map (open addressing)
// ---------------------------------------------------------------------------

struct BlobEntry {
    const char* key;
    uint32_t keyLen;
    uint64_t size;
    uint64_t offset;
};

class FastHashMap {
public:
    explicit FastHashMap(std::size_t capacity) {
        capacity_ = 1;
        while (capacity_ < capacity * 2) capacity_ <<= 1;
        mask_ = capacity_ - 1;
        entries_.resize(capacity_);
        for (auto& e : entries_) e.key = nullptr;
    }

    void insert(const char* key, uint32_t keyLen, uint64_t size, uint64_t offset) {
        std::size_t h = hash_(std::string_view(key, keyLen)) & mask_;
        while (entries_[h].key != nullptr) {
            h = (h + 1) & mask_;
        }
        entries_[h] = {key, keyLen, size, offset};
    }

    const BlobEntry* find(const char* key, std::size_t keyLen) const {
        std::size_t h = hash_(std::string_view(key, keyLen)) & mask_;
        while (entries_[h].key != nullptr) {
            if (entries_[h].keyLen == keyLen &&
                std::memcmp(entries_[h].key, key, keyLen) == 0) {
                return &entries_[h];
            }
            h = (h + 1) & mask_;
        }
        return nullptr;
    }

private:
    std::vector<BlobEntry> entries_;
    std::size_t capacity_;
    std::size_t mask_;
    FNV1aHash hash_;
};

// ---------------------------------------------------------------------------
// io_uring async output writer
// ---------------------------------------------------------------------------

class IOUringWriter {
public:
    static constexpr int QUEUE_DEPTH = 32;
    static constexpr std::size_t BUF_SIZE = 64 * 1024;

    IOUringWriter(int fd) : fd_(fd), pending_(0) {
        if (io_uring_queue_init(QUEUE_DEPTH, &ring_, 0) < 0) {
            fallback_ = true;
            return;
        }
        fallback_ = false;

        // Get buffers from pool
        for (int i = 0; i < 2; ++i) {
            buffers_[i] = g_bufferPool.get();
        }
        currentBuf_ = 0;
    }

    ~IOUringWriter() {
        flush();
        drain();
        if (!fallback_) {
            io_uring_queue_exit(&ring_);
        }
        for (int i = 0; i < 2; ++i) {
            if (buffers_[i]) g_bufferPool.put(buffers_[i]);
        }
    }

    void write(const char* data, std::size_t len) {
        if (fallback_) {
            ::write(fd_, data, len);
            return;
        }

        Buffer* buf = buffers_[currentBuf_];
        if (buf->size + len > Buffer::CAPACITY) {
            submitBuffer();
            buf = buffers_[currentBuf_];
        }
        std::memcpy(buf->data + buf->size, data, len);
        buf->size += len;
    }

    void writeU64(uint64_t x) {
        char tmp[24];
        int len = 0;
        if (x == 0) {
            write("0", 1);
            return;
        }
        while (x > 0) {
            tmp[len++] = '0' + (x % 10);
            x /= 10;
        }
        char out[24];
        for (int i = 0; i < len; ++i) {
            out[i] = tmp[len - 1 - i];
        }
        write(out, len);
    }

    void writeChar(char c) { write(&c, 1); }
    void writeStr(const char* s) { write(s, std::strlen(s)); }

    void flush() {
        if (fallback_) return;
        Buffer* buf = buffers_[currentBuf_];
        if (buf->size > 0) {
            submitBuffer();
        }
    }

private:
    int fd_;
    io_uring ring_;
    bool fallback_;
    Buffer* buffers_[2];
    int currentBuf_;
    int pending_;

    void submitBuffer() {
        Buffer* buf = buffers_[currentBuf_];
        if (buf->size == 0) return;

        // Wait if too many pending
        while (pending_ >= QUEUE_DEPTH - 1) {
            drainOne();
        }

        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_write(sqe, fd_, buf->data, buf->size, -1);
        io_uring_sqe_set_data(sqe, buf);
        io_uring_submit(&ring_);
        pending_++;

        // Switch to other buffer
        currentBuf_ = 1 - currentBuf_;
        buffers_[currentBuf_]->reset();
    }

    void drainOne() {
        struct io_uring_cqe* cqe;
        if (io_uring_wait_cqe(&ring_, &cqe) == 0) {
            io_uring_cqe_seen(&ring_, cqe);
            pending_--;
        }
    }

    void drain() {
        while (pending_ > 0) {
            drainOne();
        }
    }
};

// ---------------------------------------------------------------------------
// Fast parsing
// ---------------------------------------------------------------------------

inline uint64_t parseU64(const char*& p) {
    uint64_t x = 0;
    while (*p >= '0' && *p <= '9') {
        x = x * 10 + (*p - '0');
        ++p;
    }
    return x;
}

inline void skipSpaces(const char*& p) {
    while (*p == ' ' || *p == '\t') ++p;
}

inline void skipLine(const char*& p) {
    while (*p && *p != '\n') ++p;
    if (*p == '\n') ++p;
}

// ---------------------------------------------------------------------------
// Parsed blob structure
// ---------------------------------------------------------------------------

struct ParsedBlob {
    const char* keyStart;
    uint32_t keyLen;
    uint64_t size;
    uint64_t offset;
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    // Memory-map stdin
    struct stat st;
    if (fstat(STDIN_FILENO, &st) != 0 || st.st_size == 0) {
        fprintf(stderr, "Error: stdin must be a file for mmap\n");
        return 1;
    }

    std::size_t fileSize = static_cast<std::size_t>(st.st_size);
    char* data = static_cast<char*>(mmap(nullptr, fileSize, PROT_READ,
                                          MAP_PRIVATE | MAP_POPULATE, STDIN_FILENO, 0));
    if (data == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    madvise(data, fileSize, MADV_SEQUENTIAL | MADV_WILLNEED);

    const char* p = data;

    // Parse N
    uint64_t n = parseU64(p);
    skipLine(p);

    // Arena for keys
    Arena arena(n * 40 + 1024 * 1024);

    // Parse all blobs
    std::vector<ParsedBlob> blobs(n);

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
    const uint64_t chunkSize = 8192;

    auto worker = [&]() {
        while (true) {
            uint64_t start = idx.fetch_add(chunkSize, std::memory_order_relaxed);
            if (start >= n) break;
            uint64_t stop = std::min(start + chunkSize, n);
            for (uint64_t i = start; i < stop; ++i) {
                char* key = arena.alloc(blobs[i].keyStart, blobs[i].keyLen);
                blobs[i].keyStart = key;
            }
        }
    };

    for (unsigned t = 0; t < numThreads; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();

    // Build hash map
    FastHashMap hashMap(n);
    for (uint64_t i = 0; i < n; ++i) {
        hashMap.insert(blobs[i].keyStart, blobs[i].keyLen, blobs[i].size, blobs[i].offset);
    }

    // Parse Q
    uint64_t q = parseU64(p);
    skipLine(p);

    // Process queries with io_uring writer
    IOUringWriter out(STDOUT_FILENO);

    const char* end = data + fileSize;
    for (uint64_t i = 0; i < q; ++i) {
        const char* keyStart = p;
        while (p < end && *p > ' ') ++p;
        std::size_t keyLen = static_cast<std::size_t>(p - keyStart);
        skipLine(p);

        const BlobEntry* e = hashMap.find(keyStart, keyLen);
        if (e) {
            out.writeU64(e->size);
            out.writeChar(' ');
            out.writeU64(e->offset);
            out.writeChar('\n');
        } else {
            out.writeStr("NOTFOUND\n");
        }
    }

    out.flush();
    munmap(data, fileSize);
    return 0;
}
