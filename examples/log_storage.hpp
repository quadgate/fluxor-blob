#pragma once

#include "../include/blob_indexer.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <regex>
#include <mutex>

namespace logstorage {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

struct LogEntry {
    uint64_t timestamp;
    LogLevel level;
    std::string service;
    std::string message;
    std::string hostname;
    std::string thread_id;
};

class LogStorage {
public:
    explicit LogStorage(const std::string& root);

    // Write log entry
    void log(const LogEntry& entry);
    void log(LogLevel level, const std::string& service, const std::string& message);

    // Query logs
    std::vector<LogEntry> query(const std::string& service, 
                                const std::string& dateStart,
                                const std::string& dateEnd,
                                LogLevel minLevel = LogLevel::DEBUG);

    // Search logs by pattern
    std::vector<LogEntry> search(const std::string& pattern,
                                 const std::string& dateStart,
                                 const std::string& dateEnd);

    // Get stats
    struct Stats {
        uint64_t totalLogs = 0;
        uint64_t totalBytes = 0;
        std::map<std::string, uint64_t> logsByService;
        std::map<LogLevel, uint64_t> logsByLevel;
    };
    Stats getStats();

    // Rotate old logs (compress and archive)
    void rotate(int daysToKeep);

    // Tail recent logs (like tail -f)
    std::vector<LogEntry> tail(const std::string& service, size_t n = 100);

private:
    blobstore::IndexedBlobStorage store_;
    std::mutex mutex_;

    std::string makeKey(const LogEntry& entry);
    std::string dateString(uint64_t timestamp);
    LogEntry parseEntry(const std::string& data);
    std::string serializeEntry(const LogEntry& entry);
    std::string levelToString(LogLevel level);
    LogLevel stringToLevel(const std::string& s);
};

} // namespace logstorage
