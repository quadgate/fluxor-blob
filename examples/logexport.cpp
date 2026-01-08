#include "log_storage.hpp"
#include <iostream>
#include <iomanip>

using namespace logstorage;

void printUsage() {
    std::cerr << "Usage: logexport <root> <command> [args]\n\n"
              << "Commands:\n"
              << "  cat <service> <date>              - Stream all logs (for piping)\n"
              << "  query <service> <start> <end>     - Query time range\n"
              << "  tail <service> <n>                - Last N logs\n"
              << "  services                          - List all services\n\n"
              << "Output format (tab-separated):\n"
              << "  timestamp\\tlevel\\tservice\\thost\\tthread\\tmessage\n\n"
              << "Examples:\n"
              << "  # Grep for errors\n"
              << "  logexport /var/log/app cat web-server 20260108 | grep ERROR\n\n"
              << "  # Count by level with awk\n"
              << "  logexport /var/log/app cat web-server 20260108 | awk -F'\\t' '{print $2}' | sort | uniq -c\n\n"
              << "  # Filter by time range with awk\n"
              << "  logexport /var/log/app cat web-server 20260108 | awk -F'\\t' '$1 > 1704672000'\n\n"
              << "  # Complex awk pattern\n"
              << "  logexport /var/log/app cat api 20260108 | awk -F'\\t' '$2==\"ERROR\" && $6 ~ /timeout/'\n";
}

void exportEntry(const LogEntry& entry) {
    std::cout << entry.timestamp << "\t"
              << (entry.level == LogLevel::DEBUG ? "DEBUG" :
                  entry.level == LogLevel::INFO ? "INFO" :
                  entry.level == LogLevel::WARN ? "WARN" :
                  entry.level == LogLevel::ERROR ? "ERROR" : "FATAL")
              << "\t" << entry.service
              << "\t" << entry.hostname
              << "\t" << entry.thread_id
              << "\t" << entry.message << "\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    std::string root = argv[1];
    std::string cmd = argv[2];

    try {
        LogStorage logs(root);

        if (cmd == "cat") {
            if (argc != 5) {
                std::cerr << "Usage: logexport <root> cat <service> <date>\n";
                return 1;
            }
            std::string service = argv[3];
            std::string date = argv[4];
            
            // Stream all logs for the date (suitable for piping)
            auto results = logs.query(service, date, date, LogLevel::DEBUG);
            for (const auto& entry : results) {
                exportEntry(entry);
            }

        } else if (cmd == "query") {
            if (argc != 6) {
                std::cerr << "Usage: logexport <root> query <service> <start> <end>\n";
                return 1;
            }
            std::string service = argv[3];
            std::string start = argv[4];
            std::string end = argv[5];
            
            auto results = logs.query(service, start, end, LogLevel::DEBUG);
            for (const auto& entry : results) {
                exportEntry(entry);
            }

        } else if (cmd == "tail") {
            if (argc != 5) {
                std::cerr << "Usage: logexport <root> tail <service> <n>\n";
                return 1;
            }
            std::string service = argv[3];
            size_t n = std::stoul(argv[4]);
            
            auto results = logs.tail(service, n);
            for (const auto& entry : results) {
                exportEntry(entry);
            }

        } else if (cmd == "services") {
            // List all unique services
            auto stats = logs.getStats();
            for (const auto& [service, count] : stats.logsByService) {
                std::cout << service << "\t" << count << "\n";
            }

        } else {
            std::cerr << "Unknown command: " << cmd << "\n";
            printUsage();
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
