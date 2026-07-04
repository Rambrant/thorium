#include "core/verify.hpp"

#include <string>

#include "core/logger.hpp"

namespace core::detail
{
    void reportResult( std::string_view group,
                        std::string_view id,
                        std::string_view description,
                        bool             passed )
    {
        static const core::Logger logger( "core::Verify");

        logger.log( passed ? core::LogLevel::Info : core::LogLevel::Error,
                    std::string(group) + "::" + std::string(id) +
                    " (" + std::string(description) + ") " +
                    (passed ? "[PASS]" : "[FAIL]"));
    }
} // namespace core::detail