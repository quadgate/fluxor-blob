// Fast Blob Indexer - Competitive Programming Style
// Optimized for: N ≤ 10^6 blobs, Q ≤ 10^5 queries
// Target: < 1 second on typical hardware

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// Fast I/O reader (getchar_unlocked on Linux, getchar on others)
// ---------------------------------------------------------------------------

#ifdef __linux__
#define GETCHAR getchar_unlocked
#define PUTCHAR putchar_unlocked
#else
#define GETCHAR getchar
#define PUTCHAR putchar
#endif

namespace fastio {

inline int readChar() {
    return GETCHAR();
}

inline void writeChar(char c) {
    PUTCHAR(c);
}

// Read non-negative 64-bit integer.
inline uint64_t readU64() {
    uint64_t x = 0;
    int c = readChar();
    while (c <= ' ') c = readChar();
    while (c > ' ') {
        x = x * 10 + (c - '0');
        c = readChar();
    }
    return x;
}

// Read string into buffer, return length.
inline int readString(char* buf) {
    int c = readChar();
    while (c <= ' ') c = readChar();
    int len = 0;
    while (c > ' ') {
        buf[len++] = static_cast<char>(c);
        c = readChar();
    }
    buf[len] = '\0';
    return len;
}

// Write 64-bit unsigned integer.
inline void writeU64(uint64_t x) {
    char buf[24];
    int pos = 0;
    if (x == 0) {
        writeChar('0');
        return;
    }
    while (x > 0) {
        buf[pos++] = '0' + (x % 10);
        x /= 10;
    }
    while (pos > 0) {
        writeChar(buf[--pos]);
    }
}

inline void writeString(const char* s) {
    while (*s) writeChar(*s++);
}

inline void newline() {
    writeChar('\n');
}

} // namespace fastio

// ---------------------------------------------------------------------------
// Blob metadata
// ---------------------------------------------------------------------------

struct BlobInfo {
    uint64_t size;
    uint64_t offset;
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    using namespace fastio;

    // Hash map: key -> (size, offset)
    std::unordered_map<std::string, BlobInfo> index;
    index.reserve(1 << 20); // Reserve for ~1M entries (reduces rehashing)

    char keyBuf[64];

    // Read N blobs
    uint64_t n = readU64();
    for (uint64_t i = 0; i < n; ++i) {
        readString(keyBuf);
        uint64_t sz = readU64();
        uint64_t off = readU64();
        index[keyBuf] = {sz, off};
    }

    // Read Q queries
    uint64_t q = readU64();
    for (uint64_t i = 0; i < q; ++i) {
        readString(keyBuf);
        auto it = index.find(keyBuf);
        if (it != index.end()) {
            writeU64(it->second.size);
            writeChar(' ');
            writeU64(it->second.offset);
            newline();
        } else {
            writeString("NOTFOUND");
            newline();
        }
    }

    return 0;
}
