// HYPER Blob Indexer - Parallel query processing
// - Parallel query execution
// - Pre-hash all queries
// - Batch prefetch
// - All previous optimizations
//
// Target: < 0.15s for N=10^6, Q=10^5

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <immintrin.h>

// SIMD memcmp
#ifdef __AVX2__
inline bool simd_eq(const char* a, const char* b, std::size_t len) {
    while (len >= 32) {
        __m256i va = _mm256_loadu_si256((const __m256i*)a);
        __m256i vb = _mm256_loadu_si256((const __m256i*)b);
        if (_mm256_movemask_epi8(_mm256_cmpeq_epi8(va, vb)) != -1) return false;
        a += 32; b += 32; len -= 32;
    }
    return len == 0 || std::memcmp(a, b, len) == 0;
}
#else
inline bool simd_eq(const char* a, const char* b, std::size_t len) { return std::memcmp(a, b, len) == 0; }
#endif

// FNV-1a
inline uint64_t fnv(const char* d, std::size_t len) {
    uint64_t h = 14695981039346656037ULL;
    while (len >= 8) {
        h = (h ^ (uint8_t)d[0]) * 1099511628211ULL;
        h = (h ^ (uint8_t)d[1]) * 1099511628211ULL;
        h = (h ^ (uint8_t)d[2]) * 1099511628211ULL;
        h = (h ^ (uint8_t)d[3]) * 1099511628211ULL;
        h = (h ^ (uint8_t)d[4]) * 1099511628211ULL;
        h = (h ^ (uint8_t)d[5]) * 1099511628211ULL;
        h = (h ^ (uint8_t)d[6]) * 1099511628211ULL;
        h = (h ^ (uint8_t)d[7]) * 1099511628211ULL;
        d += 8; len -= 8;
    }
    while (len--) h = (h ^ (uint8_t)*d++) * 1099511628211ULL;
    return h;
}

// Arena
class Arena {
    char* data_; std::size_t cap_; std::atomic<std::size_t> off_{0};
public:
    Arena(std::size_t c) : cap_(c) {
        data_ = (char*)mmap(0, c, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        madvise(data_, c, MADV_HUGEPAGE);
    }
    ~Arena() { munmap(data_, cap_); }
    char* alloc(const char* s, std::size_t l) {
        std::size_t o = off_.fetch_add(l+1, std::memory_order_relaxed);
        if (o+l+1 > cap_) return nullptr;
        char* p = data_+o; std::memcpy(p, s, l); p[l]=0; return p;
    }
};

// Hash entry
struct alignas(32) Entry { const char* k; uint32_t kl, h32; uint64_t sz, off; };

// HashMap
class HashMap {
    Entry* e_; std::size_t cap_, mask_;
public:
    HashMap(std::size_t n) {
        cap_ = 1; while (cap_ < n*2) cap_ <<= 1; mask_ = cap_-1;
        e_ = (Entry*)mmap(0, cap_*sizeof(Entry), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        madvise(e_, cap_*sizeof(Entry), MADV_HUGEPAGE);
        for (std::size_t i = 0; i < cap_; ++i) e_[i].k = nullptr;
    }
    ~HashMap() { munmap(e_, cap_*sizeof(Entry)); }
    
    void put(const char* k, uint32_t kl, uint64_t sz, uint64_t off) {
        uint64_t h = fnv(k, kl);
        uint32_t h32 = h;
        std::size_t i = h & mask_;
        while (e_[i].k) i = (i+1) & mask_;
        e_[i] = {k, kl, h32, sz, off};
    }
    
    void prefetch(uint64_t h) const { __builtin_prefetch(&e_[h & mask_], 0, 3); }
    
    const Entry* get(const char* k, uint32_t kl, uint64_t h) const {
        uint32_t h32 = h;
        std::size_t i = h & mask_;
        while (e_[i].k) {
            if (e_[i].h32 == h32 && e_[i].kl == kl && simd_eq(e_[i].k, k, kl))
                return &e_[i];
            i = (i+1) & mask_;
        }
        return nullptr;
    }
};

// Query result
struct QResult { uint64_t sz, off; bool found; };

// Parsing
inline uint64_t pu64(const char*& p) { uint64_t x=0; while (*p>='0'&&*p<='9') x=x*10+(*p++-'0'); return x; }
inline void skip(const char*& p) { while (*p==' '||*p=='\t') ++p; }
inline void line(const char*& p) { while (*p&&*p!='\n') ++p; if (*p) ++p; }

int main() {
    struct stat st;
    if (fstat(0, &st) || st.st_size == 0) return 1;
    std::size_t fsz = st.st_size;
    char* data = (char*)mmap(0, fsz, PROT_READ, MAP_PRIVATE|MAP_POPULATE, 0, 0);
    madvise(data, fsz, MADV_SEQUENTIAL);

    const char* p = data;
    uint64_t n = pu64(p); line(p);

    // Parse blobs
    struct Blob { const char* k; uint32_t kl; uint64_t sz, off; };
    std::vector<Blob> blobs(n);
    for (uint64_t i = 0; i < n; ++i) {
        const char* k = p;
        while (*p > ' ') ++p;
        uint32_t kl = p - k;
        skip(p); uint64_t sz = pu64(p);
        skip(p); uint64_t off = pu64(p);
        line(p);
        blobs[i] = {k, kl, sz, off};
    }

    // Arena copy (parallel)
    Arena arena(n * 40 + 4*1024*1024);
    unsigned nT = std::min(8u, std::thread::hardware_concurrency());
    std::vector<std::thread> th;
    std::atomic<uint64_t> idx{0};
    for (unsigned t = 0; t < nT; ++t) {
        th.emplace_back([&]() {
            for (;;) {
                uint64_t s = idx.fetch_add(8192);
                if (s >= n) break;
                for (uint64_t i = s; i < std::min(s+8192, n); ++i)
                    blobs[i].k = arena.alloc(blobs[i].k, blobs[i].kl);
            }
        });
    }
    for (auto& t : th) t.join();

    // Build hash map
    HashMap hm(n);
    for (uint64_t i = 0; i < n; ++i)
        hm.put(blobs[i].k, blobs[i].kl, blobs[i].sz, blobs[i].off);

    // Parse queries + hash
    uint64_t q = pu64(p); line(p);
    struct Query { const char* k; uint32_t kl; uint64_t h; };
    std::vector<Query> qs(q);
    const char* end = data + fsz;
    for (uint64_t i = 0; i < q; ++i) {
        const char* k = p;
        while (p < end && *p > ' ') ++p;
        uint32_t kl = p - k;
        qs[i] = {k, kl, fnv(k, kl)};
        line(p);
    }

    // Parallel query execution
    std::vector<QResult> results(q);
    std::atomic<uint64_t> qidx{0};
    th.clear();
    
    for (unsigned t = 0; t < nT; ++t) {
        th.emplace_back([&]() {
            for (;;) {
                uint64_t s = qidx.fetch_add(1024);
                if (s >= q) break;
                uint64_t e = std::min(s + 1024, q);
                
                // Prefetch batch
                for (uint64_t i = s; i < std::min(s + 32, e); ++i)
                    hm.prefetch(qs[i].h);
                
                for (uint64_t i = s; i < e; ++i) {
                    if (i + 16 < e) hm.prefetch(qs[i + 16].h);
                    const Entry* r = hm.get(qs[i].k, qs[i].kl, qs[i].h);
                    if (r) results[i] = {r->sz, r->off, true};
                    else results[i] = {0, 0, false};
                }
            }
        });
    }
    for (auto& t : th) t.join();

    // Output (single thread, sequential)
    char buf[256 * 1024];
    std::size_t pos = 0;
    
    auto flush = [&]() { if (pos) { write(1, buf, pos); pos = 0; } };
    auto wc = [&](char c) { buf[pos++] = c; if (pos >= sizeof(buf) - 64) flush(); };
    auto wu = [&](uint64_t x) {
        char t[24]; int l = 0;
        if (x == 0) { wc('0'); return; }
        while (x) { t[l++] = '0' + x % 10; x /= 10; }
        while (l--) wc(t[l]);
    };
    auto ws = [&](const char* s, std::size_t l) {
        std::memcpy(buf + pos, s, l); pos += l;
        if (pos >= sizeof(buf) - 64) flush();
    };

    for (uint64_t i = 0; i < q; ++i) {
        if (results[i].found) {
            wu(results[i].sz); wc(' '); wu(results[i].off); wc('\n');
        } else {
            ws("NOTFOUND\n", 9);
        }
    }
    flush();

    munmap(data, fsz);
    return 0;
}
