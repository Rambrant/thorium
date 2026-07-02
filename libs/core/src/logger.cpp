#include "core/logger.hpp"

#include <iostream>

namespace core {

Logger::Logger(std::string_view name) : name_(name) {}

void Logger::log(LogLevel level, std::string_view message) const {
    std::cout << "[" << to_string(level) << "] " << name_ << ": " << message << "\n";
}

std::string_view to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace core
