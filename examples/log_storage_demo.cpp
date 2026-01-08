#include "log_storage.hpp"
#include <iostream>
#include <thread>
#include <random>

using namespace logstorage;

void printEntry(const LogEntry& entry) {
    time_t t = entry.timestamp;
    struct tm* tm_info = std::localtime(&t);
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", tm_info);
    
    std::cout << "[" << timeStr << "] "
              << "[" << entry.hostname << ":" << entry.thread_id << "] "
              << entry.service << " "
              << (entry.level == LogLevel::ERROR ? "ERROR" :
                  entry.level == LogLevel::WARN ? "WARN" :
                  entry.level == LogLevel::INFO ? "INFO" :
                  entry.level == LogLevel::DEBUG ? "DEBUG" : "FATAL")
              << ": " << entry.message << "\n";
}

void simulateService(LogStorage& logs, const std::string& service, int numLogs) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> levelDist(0, 4);
    std::uniform_int_distribution<> actionDist(0, 5);
    
    const char* actions[] = {
        "Request processed",
        "Connection established",
        "Query executed",
        "Cache hit",
        "Task completed",
        "Health check passed"
    };
    
    const char* errors[] = {
        "Connection timeout",
        "Database error",
        "Invalid request",
        "Resource exhausted",
        "Service unavailable"
    };
    
    for (int i = 0; i < numLogs; ++i) {
        int level = levelDist(gen);
        LogLevel logLevel = static_cast<LogLevel>(level);
        
        std::string message;
        if (logLevel >= LogLevel::ERROR) {
            message = errors[actionDist(gen) % 5];
        } else {
            message = actions[actionDist(gen) % 6];
        }
        
        logs.log(logLevel, service, message + " [req_" + std::to_string(i) + "]");
        
        // Small delay
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main() {
    std::cout << "=== Log Storage Demo ===\n\n";
    
    // Initialize storage
    LogStorage logs("/tmp/logstorage_demo");
    
    std::cout << "1. Writing logs from multiple services...\n";
    
    std::vector<std::thread> threads;
    threads.emplace_back(simulateService, std::ref(logs), "web-server", 50);
    threads.emplace_back(simulateService, std::ref(logs), "api-gateway", 50);
    threads.emplace_back(simulateService, std::ref(logs), "worker", 50);
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "Written 150 log entries\n\n";
    
    // Query logs
    std::cout << "2. Querying ERROR logs from web-server...\n";
    auto errors = logs.query("web-server", "20260101", "20261231", LogLevel::ERROR);
    std::cout << "Found " << errors.size() << " errors:\n";
    for (size_t i = 0; i < std::min(size_t(5), errors.size()); ++i) {
        printEntry(errors[i]);
    }
    std::cout << "\n";
    
    // Search logs
    std::cout << "3. Searching for 'timeout' in all logs...\n";
    auto timeouts = logs.search("timeout", "20260101", "20261231");
    std::cout << "Found " << timeouts.size() << " matching entries:\n";
    for (const auto& entry : timeouts) {
        printEntry(entry);
    }
    std::cout << "\n";
    
    // Tail logs
    std::cout << "4. Tailing last 10 logs from api-gateway...\n";
    auto recent = logs.tail("api-gateway", 10);
    for (const auto& entry : recent) {
        printEntry(entry);
    }
    std::cout << "\n";
    
    // Statistics
    std::cout << "5. Storage statistics:\n";
    auto stats = logs.getStats();
    std::cout << "  Total logs: " << stats.totalLogs << "\n";
    std::cout << "  Total size: " << (stats.totalBytes / 1024.0) << " KB\n";
    std::cout << "  Logs by service:\n";
    for (const auto& [service, count] : stats.logsByService) {
        std::cout << "    " << service << ": " << count << "\n";
    }
    std::cout << "  Logs by level:\n";
    for (const auto& [level, count] : stats.logsByLevel) {
        std::cout << "    " << (level == LogLevel::ERROR ? "ERROR" :
                                level == LogLevel::WARN ? "WARN" :
                                level == LogLevel::INFO ? "INFO" :
                                level == LogLevel::DEBUG ? "DEBUG" : "FATAL")
                  << ": " << count << "\n";
    }
    std::cout << "\n";
    
    // Rotation demo
    std::cout << "6. Rotating logs (keeping last 30 days)...\n";
    logs.rotate(30);
    std::cout << "Rotation complete\n\n";
    
    std::cout << "Demo complete! Storage at: /tmp/logstorage_demo\n";
    
    return 0;
}
