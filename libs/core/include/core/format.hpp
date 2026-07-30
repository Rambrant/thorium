#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/quantity.hpp"
#include "core/quantity_kind.hpp"

namespace core
{
    //
    // Turning a value into log text. Both log streams (see core/journal.hpp)
    // need the same three things about every value they record -- its printable
    // form, its bare number, and its unit -- so all three live here rather
    // than being re-derived per sink, which is how the RTF and the SARIF log
    // are guaranteed to be describing the same reading rather than two
    // independently-formatted approximations of it.
    //
    // Split from core/quantity_kind.hpp on purpose, even though unitSymbol()
    // is arguably a fact about a QuantityKind: that header is what the session
    // seam (core/session.hpp) and the recording format are built on, and
    // neither has any business growing a dependency on how a human wants a
    // number spelled. Nothing here is needed to *take* a measurement, only to
    // report one.
    //

    //
    // Fixed at 6 significant digits (%.6g) -- what ostream's default double
    // formatting already produces, kept as an explicit choice here because a
    // test log's numbers end up in a report somebody reads: 5.021, not
    // 5.0209999999999999. Small enough magnitudes fall back to exponent form
    // ("1.2e-06" for a rise time in seconds), which is the right answer for a
    // log rather than a screenful of leading zeros.
    //
    [[nodiscard]]
    auto formatNumber( double value) -> std::string;

    //
    // The unit's printed symbol, per QuantityKind. Deliberately NOT derived by
    // reflecting over the Unit tag type's own name the way core::meta::
    // to_string() derives enumerator names elsewhere in this codebase: that
    // mapping isn't mechanical (time_Type -> "s", PF_Type -> nothing at all),
    // so a reflected version would need a correction table for most of the
    // list, which is strictly worse than just being the table.
    //
    // "Ohm" rather than the Greek symbol, matching the _Ohm literal's own
    // spelling in core::literals -- these strings reach an RTF file, where a
    // non-ASCII byte needs escaping, for no gain over the spelling the
    // criteria files already use.
    //
    // PowerFactor returns an empty string: it is dimensionless (see
    // core::quantities' own comment on why it is still its own Quantity), and
    // "0.95" with nothing after it is the correct rendering.
    //
    [[nodiscard]]
    auto unitSymbol( QuantityKind kind) -> std::string_view;

    //
    // A QuantityVariant as "<number> <unit>" -- the runtime-kind counterpart
    // to describeValue() below, for the one seam that carries a value without
    // its concrete type (core::MeasureEngine, via ISession::fetch).
    //
    [[nodiscard]]
    auto formatQuantity( const QuantityVariant & value) -> std::string;

    //
    // Hex rendering for integral values, minimum width, uppercase digits
    // ("0xF5"). Takes the widest unsigned type so describeValue() below can
    // funnel any integral through one non-template function.
    //
    [[nodiscard]]
    auto formatHex( std::uint64_t value) -> std::string;

    //
    // The three value questions a log asks at a core::Verify call site, where
    // the value's type is whatever the criterion's predicate accepts -- a
    // Quantity<Unit>, a raw double, or an integer register readback. Each is
    // written once, as an if-constexpr chain over that type, so a criterion
    // against a new kind of value gets logged sensibly (or, at worst, silently
    // without a value) rather than failing to compile.
    //

    // The unit symbol for T, or empty for anything that isn't a Quantity<Unit>.
    template<typename T>
    [[nodiscard]]
    auto unitOf() -> std::string_view
    {
        if constexpr( quantities::QuantityType<T>)
        {
            return unitSymbol( quantityKindOf<T>());
        }
        else
        {
            return {};
        }
    }

    //
    // T's bare number, for the machine-readable log's property bag -- so a
    // tool consuming the SARIF can compare readings numerically without
    // re-parsing describeValue()'s text. std::nullopt for anything with no
    // meaningful number (bool included: "true" is not 1.0 in a report).
    //
    template<typename T>
    [[nodiscard]]
    auto numericOf( const T & value) -> std::optional<double>
    {
        if constexpr( quantities::QuantityType<T>)
        {
            return value.value();
        }
        else if constexpr( std::same_as<T, bool>)
        {
            return std::nullopt;
        }
        else if constexpr( std::integral<T> || std::floating_point<T>)
        {
            return static_cast<double>( value);
        }
        else
        {
            return std::nullopt;
        }
    }

    //
    // T as log text. Integers are given in both bases ("245 (0xF5)") rather
    // than one: a fuse/status register readback is authored in hex in the
    // criteria file (see dut/criteria_production.inc's MASK/EQ values) but
    // reads as a decimal in any default formatting, and a log that shows only
    // one of the two forces whoever is reading it to convert by hand to check
    // it against the criterion.
    //
    template<typename T>
    [[nodiscard]]
    auto describeValue( const T & value) -> std::string
    {
        if constexpr( quantities::QuantityType<T>)
        {
            const auto unit = unitSymbol( quantityKindOf<T>());

            return unit.empty() ? formatNumber( value.value())
                                : formatNumber( value.value()) + ' ' + std::string( unit);
        }
        else if constexpr( std::same_as<T, bool>)
        {
            return value ? "true" : "false";
        }
        else if constexpr( std::integral<T>)
        {
            //
            // Through make_unsigned first, so a negative value's hex is its
            // two's-complement pattern rather than an implementation-defined
            // conversion of a negative to std::uint64_t.
            //
            const auto pattern = static_cast<std::uint64_t>( static_cast<std::make_unsigned_t<T>>( value));

            return std::to_string( value) + " (" + formatHex( pattern) + ")";
        }
        else if constexpr( std::floating_point<T>)
        {
            return formatNumber( static_cast<double>( value));
        }
        else
        {
            return {};
        }
    }
} // namespace core
