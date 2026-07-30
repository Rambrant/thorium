#include "core/format.hpp"

#include <array>
#include <cstdio>
#include <variant>

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

    //
    // No default case, deliberately -- -Wswitch (via -Wall -Werror) is what
    // makes a QuantityKind added to the enum a build failure here rather than
    // a value that silently logs without its unit. Same shape, and the same
    // reasoning, as core::quantityVariantFromKind in core/quantity_kind.cpp.
    //
    auto unitSymbol( const QuantityKind kind) -> std::string_view
    {
        switch( kind)
        {
            case QuantityKind::Voltage:       return "V";
            case QuantityKind::Current:       return "A";
            case QuantityKind::Power:         return "W";
            case QuantityKind::ApparentPower: return "VA";
            case QuantityKind::Resistance:    return "Ohm";
            case QuantityKind::Time:          return "s";
            case QuantityKind::Decibel:       return "dB";
            case QuantityKind::Frequency:     return "Hz";
            case QuantityKind::PowerFactor:   return "";     // dimensionless -- see unitSymbol's own comment
            case QuantityKind::ReactivePower: return "var";
        }

        return "?";
    }

    auto formatQuantity( const QuantityVariant & value) -> std::string
    {
        //
        // index() -> QuantityKind is the same alternative-order guarantee
        // core/quantity_kind.cpp static_asserts; nothing here needs to know
        // which of the ten units is live beyond naming its symbol.
        //
        const auto unit = unitSymbol( static_cast<QuantityKind>( value.index()));
        const auto text = formatNumber( rawValue( value));

        return unit.empty() ? text : text + ' ' + std::string( unit);
    }
} // namespace core
