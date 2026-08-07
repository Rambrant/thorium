#include "core/format.hpp"

#include <array>
#include <cstdio>

namespace core
{
    auto formatNumber( const double value) -> std::string
    {
        //
        // snprintf rather than an ostringstream: this runs once per logged
        // value on both streams, and it needs no locale, no stream state to
        // save and restore, and no allocation beyond the result itself. The
        // buffer is sized for the longest %.6g can produce (sign, 6 digits,
        // point, "e-308") with room to spare.
        //
        std::array<char, 32> buffer{};

        const auto written = std::snprintf( buffer.data(), buffer.size(), "%.6g", value);

        return ( written > 0) ? std::string( buffer.data(), static_cast<std::size_t>( written)) : std::string{};
    }

    auto formatHex( const std::uint64_t value) -> std::string
    {
        std::array<char, 32> buffer{};

        const auto written = std::snprintf( buffer.data(), buffer.size(), "0x%llX", static_cast<unsigned long long>( value));

        return ( written > 0) ? std::string( buffer.data(), static_cast<std::size_t>( written)) : std::string{};
    }

} // namespace core
