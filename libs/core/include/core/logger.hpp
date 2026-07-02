#pragma once

#include <string>
#include <string_view>

namespace core {

enum class LogLevel { Debug, Info, Warn, Error };

// Minimal logging utility. Real projects would expand this
// (sinks, formatting, thread-safety, etc.) but the shape stays
// the same: core has zero dependencies on the layers above it.
class Logger {
public:
    explicit Logger(std::string_view name);

    void log(LogLevel level, std::string_view message) const;

    [[nodiscard]] std::string name() const { return name_; }

private:
    std::string name_;
};

[[nodiscard]] std::string_view to_string(LogLevel level);

}  // namespace core
