#pragma once

#include <string>
#include <string_view>
#include <fmt/format.h>

namespace flint::logging {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

void init(const std::string& logFilePath);
void set_level(LogLevel level);
void log(LogLevel level, std::string_view message);

template <typename... Args>
void debug(fmt::format_string<Args...> fmt, Args&&... args) {
    log(LogLevel::Debug, fmt::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void info(fmt::format_string<Args...> fmt, Args&&... args) {
    log(LogLevel::Info, fmt::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(fmt::format_string<Args...> fmt, Args&&... args) {
    log(LogLevel::Warning, fmt::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void error(fmt::format_string<Args...> fmt, Args&&... args) {
    log(LogLevel::Error, fmt::format(fmt, std::forward<Args>(args)...));
}

} // namespace flint::logging
