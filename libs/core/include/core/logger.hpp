#pragma once

#include <string>
#include <string_view>

namespace core
{
    enum class LogLevel { Debug, Info, Warn, Error };

    //
    // Minimal logging utility. Real projects would expand this
    // (sinks, formatting, thread-safety, etc.) but the shape stays
    // the same: core has zero dependencies on the layers above it.
    //
    class Logger
    {
        public:
            explicit Logger( std::string_view name);

            void log( LogLevel level, std::string_view message) const;

            [[nodiscard]]
            auto name() const -> std::string
            {
                return mName;
            }

        private:
            std::string mName;
    };

    [[nodiscard]]
    auto to_string( LogLevel level) -> std::string_view;
} // namespace core
