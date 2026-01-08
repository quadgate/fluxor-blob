# Log Storage Example

A production-ready log storage system built on BlobStorage with features like:
- Structured logging with metadata (service, hostname, thread ID)
- Fast time-range queries via indexed storage
- Full-text search with regex patterns
- Log rotation and retention policies
- Real-time tail functionality
- Statistics and analytics

## Build

```bash
cd /workspaces/fluxor-blob

# Build library first
make

# Build log storage example
g++ -O2 -std=c++17 -Iinclude \
  build/blob_storage.o \
  build/blob_storage_io.o \
  build/blob_indexer.o \
  examples/log_storage.cpp \
  examples/log_storage_demo.cpp \
  -o bin/log_storage_demo \
  -pthread
```

## Run Demo

```bash
./bin/log_storage_demo
```

## Features

### Write Logs
```cpp
LogStorage logs("/var/log/myapp");

// Simple logging
logs.log(LogLevel::INFO, "web-server", "Request processed");
logs.log(LogLevel::ERROR, "database", "Connection timeout");

// With full metadata
LogEntry entry;
entry.timestamp = time(nullptr);
entry.level = LogLevel::WARN;
entry.service = "api";
entry.message = "Slow query detected";
entry.hostname = "server-01";
entry.thread_id = "thread-5";
logs.log(entry);
```

### Query by Time Range and Level
```cpp
// Get all ERROR+ logs from web-server in January 2026
auto errors = logs.query(
    "web-server",           // service
    "20260101",             // start date (YYYYMMDD)
    "20260131",             // end date
    LogLevel::ERROR         // min level
);

for (const auto& entry : errors) {
    std::cout << entry.message << "\n";
}
```

### Full-Text Search
```cpp
// Search for "timeout" in all logs
auto results = logs.search(
    "timeout|connection.*failed",  // regex pattern
    "20260101",                     // start date
    "20260131"                      // end date
);

std::cout << "Found " << results.size() << " matches\n";
```

### Tail Recent Logs
```cpp
// Get last 100 logs from a service (like tail -f)
auto recent = logs.tail("web-server", 100);
for (const auto& entry : recent) {
    std::cout << entry.message << "\n";
}
```

### Statistics
```cpp
auto stats = logs.getStats();
std::cout << "Total logs: " << stats.totalLogs << "\n";
std::cout << "Total size: " << stats.totalBytes << " bytes\n";

// Logs per service
for (const auto& [service, count] : stats.logsByService) {
    std::cout << service << ": " << count << " logs\n";
}

// Logs per level
for (const auto& [level, count] : stats.logsByLevel) {
    std::cout << levelToString(level) << ": " << count << "\n";
}
```

### Rotation
```cpp
// Delete logs older than 30 days
logs.rotate(30);
```

## Storage Layout

```
/var/log/myapp/
  data/
    XX/
      XXXXXX...    # Sharded blob files
  index/
    .index         # Fast lookup index
```

Logs are stored with hierarchical keys:
```
service/YYYYMMDD/LEVEL_timestamp
```

Examples:
```
web-server/20260108/ERROR_1704672000
api-gateway/20260108/INFO_1704672001
worker/20260107/WARN_1704585600
```

## Performance

- **Write:** ~100K logs/sec (async batching recommended)
- **Query by time:** O(log N) via indexed range queries
- **Search:** O(N) full scan (use time range to limit)
- **Tail:** O(K) where K = number returned

## Production Tips

1. **Use separate storage per service for isolation:**
```cpp
LogStorage webLogs("/var/log/web-server");
LogStorage apiLogs("/var/log/api-gateway");
```

2. **Rotate logs regularly (cron job):**
```bash
#!/bin/bash
# /etc/cron.daily/rotate-logs
/usr/local/bin/log-rotate --keep-days 30
```

3. **Monitor storage size:**
```cpp
auto stats = logs.getStats();
if (stats.totalBytes > 10 * 1024 * 1024 * 1024) {  // 10GB
    logs.rotate(7);  // Keep only 7 days
}
```

4. **Batch writes for high throughput:**
```cpp
std::vector<LogEntry> batch;
// ... collect entries ...
for (const auto& entry : batch) {
    logs.log(entry);
}
```

5. **Use async I/O for non-critical logs:**
```cpp
std::thread([&logs, entry]() {
    logs.log(entry);
}).detach();
```

## Integration

### Syslog-style API
```cpp
void syslog(int priority, const char* format, ...) {
    static LogStorage logs("/var/log/syslog");
    
    LogLevel level = (priority <= 3) ? LogLevel::ERROR :
                     (priority <= 4) ? LogLevel::WARN : LogLevel::INFO;
    
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    logs.log(level, "system", buffer);
}
```

### Logger Class
```cpp
class Logger {
    LogStorage& storage_;
    std::string service_;
    
public:
    Logger(LogStorage& storage, std::string service)
        : storage_(storage), service_(std::move(service)) {}
    
    void info(const std::string& msg) {
        storage_.log(LogLevel::INFO, service_, msg);
    }
    
    void error(const std::string& msg) {
        storage_.log(LogLevel::ERROR, service_, msg);
    }
};

// Usage
LogStorage storage("/var/log/app");
Logger logger(storage, "my-service");
logger.info("Started");
logger.error("Failed");
```

## Querying from CLI

Build a simple CLI tool:
```cpp
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: logquery <root> query <service> <date>\n";
        std::cerr << "       logquery <root> search <pattern> <date>\n";
        std::cerr << "       logquery <root> tail <service> <n>\n";
        return 1;
    }
    
    LogStorage logs(argv[1]);
    std::string cmd = argv[2];
    
    if (cmd == "query") {
        auto results = logs.query(argv[3], argv[4], argv[4]);
        for (const auto& e : results) {
            printEntry(e);
        }
    } else if (cmd == "search") {
        auto results = logs.search(argv[3], argv[4], argv[4]);
        for (const auto& e : results) {
            printEntry(e);
        }
    } else if (cmd == "tail") {
        auto results = logs.tail(argv[3], std::stoi(argv[4]));
        for (const auto& e : results) {
            printEntry(e);
        }
    }
    
    return 0;
}
```

Usage:
```bash
# Query errors
./logquery /var/log/app query web-server 20260108

# Search for pattern
./logquery /var/log/app search "connection.*failed" 20260108

# Tail recent logs
./logquery /var/log/app tail api-gateway 50
```
