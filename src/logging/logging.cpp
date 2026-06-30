#include "logging.hpp"
#include <iostream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace flint::logging {

namespace {
std::mutex g_logMutex;
std::ofstream g_logFile;
LogLevel g_maxLevel = LogLevel::Info;

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string_view level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
    }
    return "UNKNOWN";
}
} // namespace

void init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile.close();
    }
    g_logFile.open(logFilePath, std::ios::out | std::ios::app);
}

void set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_maxLevel = level;
}

void log(LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (static_cast<int>(level) < static_cast<int>(g_maxLevel)) {
        return;
    }

    std::string timestamp = get_timestamp();
    std::string_view levelStr = level_to_string(level);
    
    // Output format: [2026-06-30 23:22:00] [INFO] message
    std::string formatted = fmt::format("[{}] [{}] {}\n", timestamp, levelStr, message);

    // Print to console
    if (level == LogLevel::Error) {
        std::cerr << formatted;
    } else {
        std::cout << formatted;
    }

    // Write to file
    if (g_logFile.is_open()) {
        g_logFile << formatted;
        g_logFile.flush();
    }
}

} // namespace flint::logging
