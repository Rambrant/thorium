#include "dsl/verify.hpp"

#include <string>

#include "core/logger.hpp"

namespace dsl::detail
{
    void reportResult( std::string_view group,
                        std::string_view id,
                        std::string_view description,
                        bool             passed )
    {
        static const core::Logger logger( "dsl::Verify");

        logger.log( passed ? core::LogLevel::Info : core::LogLevel::Error,
                    std::string(group) + "::" + std::string(id) +
                    " (" + std::string(description) + ") " +
                    (passed ? "[PASS]" : "[FAIL]"));
    }
} // namespace dsl::detail
