#include "log_storage.hpp"
#include <ctime>
#include <unistd.h>
#include <algorithm>
#include <thread>

namespace logstorage {

LogStorage::LogStorage(const std::string& root) : store_(root) {
    store_.init();
    if (!store_.loadIndex()) {
        store_.rebuildIndex();
    }
}

void LogStorage::log(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = makeKey(entry);
    std::string serialized = serializeEntry(entry);
    std::vector<unsigned char> data(serialized.begin(), serialized.end());
    
    store_.put(key, data);
}

void LogStorage::log(LogLevel level, const std::string& service, 
                     const std::string& message) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now().time_since_epoch().count() / 1000000000;
    entry.level = level;
    entry.service = service;
    entry.message = message;
    
    // Get hostname
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    entry.hostname = hostname;
    
    // Get thread ID
    std::stringstream ss;
    ss << std::this_thread::get_id();
    entry.thread_id = ss.str();
    
    log(entry);
}

std::vector<LogEntry> LogStorage::query(const std::string& service,
                                        const std::string& dateStart,
                                        const std::string& dateEnd,
                                        LogLevel minLevel) {
    // Build key range
    std::string startKey = service + "/" + dateStart;
    std::string endKey = service + "/" + dateEnd + "~";  // ~ sorts after all printable chars
    
    auto keys = store_.keysInRange(startKey, endKey);
    
    std::vector<LogEntry> results;
    for (const auto& key : keys) {
        try {
            auto data = store_.get(key);
            std::string content(data.begin(), data.end());
            auto entry = parseEntry(content);
            
            if (entry.level >= minLevel) {
                results.push_back(entry);
            }
        } catch (...) {
            // Skip corrupted entries
        }
    }
    
    // Sort by timestamp
    std::sort(results.begin(), results.end(), 
              [](const LogEntry& a, const LogEntry& b) {
                  return a.timestamp < b.timestamp;
              });
    
    return results;
}

std::vector<LogEntry> LogStorage::search(const std::string& pattern,
                                         const std::string& dateStart,
                                         const std::string& dateEnd) {
    std::regex re(pattern, std::regex::icase);
    std::vector<LogEntry> results;
    
    // Get all keys in date range
    std::string startPrefix = dateStart;
    std::string endPrefix = dateEnd;
    
    auto allKeys = store_.list();
    for (const auto& key : allKeys) {
        // Check if key is in date range
        auto parts = key.find('/');
        if (parts == std::string::npos) continue;
        
        auto date = key.substr(parts + 1, 8);
        if (date >= dateStart && date <= endPrefix) {
            try {
                auto data = store_.get(key);
                std::string content(data.begin(), data.end());
                
                if (std::regex_search(content, re)) {
                    results.push_back(parseEntry(content));
                }
            } catch (...) {
                // Skip
            }
        }
    }
    
    std::sort(results.begin(), results.end(),
              [](const LogEntry& a, const LogEntry& b) {
                  return a.timestamp < b.timestamp;
              });
    
    return results;
}

LogStorage::Stats LogStorage::getStats() {
    Stats stats;
    
    auto allKeys = store_.list();
    for (const auto& key : allKeys) {
        auto meta = store_.getMeta(key);
        if (meta) {
            stats.totalLogs++;
            stats.totalBytes += meta->size;
            
            // Extract service from key
            auto pos = key.find('/');
            if (pos != std::string::npos) {
                std::string service = key.substr(0, pos);
                stats.logsByService[service]++;
            }
            
            // Parse level
            try {
                auto data = store_.get(key);
                std::string content(data.begin(), data.end());
                auto entry = parseEntry(content);
                stats.logsByLevel[entry.level]++;
            } catch (...) {
                // Skip
            }
        }
    }
    
    return stats;
}

void LogStorage::rotate(int daysToKeep) {
    auto now = std::chrono::system_clock::now().time_since_epoch().count() / 1000000000;
    uint64_t cutoff = now - (daysToKeep * 86400);
    
    auto allKeys = store_.list();
    for (const auto& key : allKeys) {
        try {
            auto data = store_.get(key);
            std::string content(data.begin(), data.end());
            auto entry = parseEntry(content);
            
            if (entry.timestamp < cutoff) {
                store_.remove(key);
            }
        } catch (...) {
            // Skip
        }
    }
    
    store_.saveIndex();
}

std::vector<LogEntry> LogStorage::tail(const std::string& service, size_t n) {
    auto allKeys = store_.keysWithPrefix(service + "/");
    
    // Sort keys in reverse (newest first)
    std::sort(allKeys.begin(), allKeys.end(), std::greater<std::string>());
    
    std::vector<LogEntry> results;
    for (const auto& key : allKeys) {
        if (results.size() >= n) break;
        
        try {
            auto data = store_.get(key);
            std::string content(data.begin(), data.end());
            results.push_back(parseEntry(content));
        } catch (...) {
            // Skip
        }
    }
    
    // Reverse to get chronological order
    std::reverse(results.begin(), results.end());
    return results;
}

std::string LogStorage::makeKey(const LogEntry& entry) {
    // Key format: service/YYYYMMDD/level_timestamp
    std::stringstream ss;
    ss << entry.service << "/"
       << dateString(entry.timestamp) << "/"
       << levelToString(entry.level) << "_"
       << std::setfill('0') << std::setw(16) << entry.timestamp;
    return ss.str();
}

std::string LogStorage::dateString(uint64_t timestamp) {
    time_t t = timestamp;
    struct tm* tm_info = std::gmtime(&t);
    char buffer[16];
    strftime(buffer, sizeof(buffer), "%Y%m%d", tm_info);
    return buffer;
}

std::string LogStorage::serializeEntry(const LogEntry& entry) {
    std::stringstream ss;
    ss << entry.timestamp << "|"
       << levelToString(entry.level) << "|"
       << entry.service << "|"
       << entry.hostname << "|"
       << entry.thread_id << "|"
       << entry.message;
    return ss.str();
}

LogEntry LogStorage::parseEntry(const std::string& data) {
    LogEntry entry;
    std::stringstream ss(data);
    std::string part;
    
    std::getline(ss, part, '|'); entry.timestamp = std::stoull(part);
    std::getline(ss, part, '|'); entry.level = stringToLevel(part);
    std::getline(ss, part, '|'); entry.service = part;
    std::getline(ss, part, '|'); entry.hostname = part;
    std::getline(ss, part, '|'); entry.thread_id = part;
    std::getline(ss, part);      entry.message = part;
    
    return entry;
}

std::string LogStorage::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
    }
    return "UNKNOWN";
}

LogLevel LogStorage::stringToLevel(const std::string& s) {
    if (s == "DEBUG") return LogLevel::DEBUG;
    if (s == "INFO")  return LogLevel::INFO;
    if (s == "WARN")  return LogLevel::WARN;
    if (s == "ERROR") return LogLevel::ERROR;
    if (s == "FATAL") return LogLevel::FATAL;
    return LogLevel::INFO;
}

} // namespace logstorage
