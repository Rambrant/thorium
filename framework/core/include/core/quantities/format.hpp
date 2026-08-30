#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/quantities/quantity.hpp"

namespace core
{
    //
    // Turning a value into log text. Both log streams (see core/journal/journal.hpp)
    // need the same three things about every value they record -- its printable
    // form, its bare number, and its unit -- so all three live here rather
    // than being re-derived per sink, which is how the RTF and the SARIF log
    // are guaranteed to be describing the same reading rather than two
    // independently-formatted approximations of it.
    //
    // Note what this file does NOT depend on: core/quantities/quantity_kind.hpp. Reporting
    // a value is a compile-time job -- every call site (core::Verify, and
    // core::MeasureEngine once it has unwrapped the session's answer) knows the
    // concrete Quantity<Unit>, and a Quantity knows its own symbol. QuantityKind
    // and QuantityVariant exist for the seams that genuinely cannot be templated
    // (a virtual session, a value read back out of a text file); routing the log
    // through them as well was machinery for a question that was never runtime.
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
    // The same number written the way a bench instrument writes it: scaled
    // into an SI prefix, so a rail is "5.021 V", a shunt drop is "50.21 mV"
    // and a rise time is "1.2 us" rather than "1.2e-06".
    //
    // Split rather than returned as one string because the prefix is not part
    // of the number and not part of the unit -- it goes between them, and the
    // only caller that can put it there is the one that knows the symbol (see
    // describeValue below). Handing back "50.21 m" would make every caller
    // re-derive where the space goes.
    //
    // Four significant digits, not six: the extra two are below the resolution
    // of every instrument on this rig, and reading them off a report is what
    // prompted this in the first place. It is deliberately a count of
    // significant digits rather than of decimal places, because the prefix has
    // already put the mantissa in [1, 1000) -- the two together are what makes
    // one rule cover a 400 V input and a 50 mV drop.
    //
    // Prefix is empty, and Mantissa is plain formatNumber output, whenever
    // prefixing would not be an improvement: a zero or a NaN, which have no
    // magnitude to take a prefix from, and a value outside the span the unit
    // declared, where the alternative is a mantissa like "0.0001" wearing a
    // prefix letter.
    //
    struct PrefixedNumber
    {
        std::string      Mantissa;
        std::string_view Prefix;
    };

    [[nodiscard]]
    auto prefixNumber( double value, quantities::SiPrefixRange range) -> PrefixedNumber;

    //
    // Hex rendering for integral values, minimum width, uppercase digits
    // ("0xF5"). Takes the widest unsigned type so describeValue() below can
    // funnel any integral through one non-template function.
    //
    [[nodiscard]]
    auto formatHex( std::uint64_t value) -> std::string;

    //
    // One octet, always two digits ("0x05", "0xF5"). Distinct from formatHex
    // above, which is minimum-width: a byte is a fixed-width field, and a
    // report is read by scanning a column of them, which a value that
    // sometimes renders one digit wide breaks.
    //
    [[nodiscard]]
    auto formatByte( std::byte value) -> std::string;

    //
    // The three value questions a log asks at a core::Verify call site, where
    // the value's type is whatever the criterion's predicate accepts -- a
    // Quantity<Unit>, a raw double, or an integer register readback. Each is
    // written once, as an if-constexpr chain over that type, so a criterion
    // against a new kind of value gets logged sensibly (or, at worst, silently
    // without a value) rather than failing to compile.
    //

    //
    // The unit symbol for T, or empty for anything that isn't a Quantity<Unit>.
    //
    // Asks the type, because the type knows: a unit tag carries its own symbol
    // (see core::quantities::Quantity::symbol in core/quantities/quantity.hpp). This used to
    // go through unitSymbol( quantityKindOf<T>()) -- an enum round-trip and an
    // array lookup to reach a value that was a compile-time constant all along.
    //
    template<typename T>
    [[nodiscard]]
    constexpr auto unitOf() -> std::string_view
    {
        if constexpr( quantities::QuantityType<T>)
        {
            return T::symbol();
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
        else if constexpr( std::same_as<T, std::byte>)
        {
            //
            // A byte read out of a serial reply is a register readback like any
            // other, so a machine consumer gets the same numeric column for it
            // that an int-typed one already had -- see describeValue below on
            // why std::byte needs naming separately at all.
            //
            return static_cast<double>( std::to_integer<unsigned char>( value));
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
            const auto unit = T::symbol();

            //
            // Prefixed if the unit says how far its scale runs, plain if it
            // does not -- see SiPrefixRange in core/quantities/quantity.hpp on
            // why saying nothing is a real answer rather than an omission.
            //
            // The prefix binds to the symbol, never to the number: "50.21 mV"
            // is one unit with a scale, not a scaled number followed by volts.
            // A prefixed unit always has a symbol, so the empty-symbol case
            // below (PowerFactor) cannot reach this branch.
            //
            if constexpr( requires { T::unitType::Prefixes; })
            {
                const auto [ mantissa, prefix] = prefixNumber( value.value(), T::unitType::Prefixes);

                return mantissa + ' ' + std::string( prefix) + std::string( unit);
            }
            else
            {
                return unit.empty() ? formatNumber( value.value())
                                    : formatNumber( value.value()) + ' ' + std::string( unit);
            }
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
        else if constexpr( std::same_as<T, std::byte>)
        {
            //
            // std::byte is deliberately not an integral type, so it does not
            // reach the branch above and has to be named here -- without which
            // a Verify against one octet of a serial reply (see
            // core::Bytes::at) would fall through to the empty default and log
            // a criterion's verdict with no value beside it.
            //
            // Two digits, always, and no decimal form: a byte is a bit pattern
            // in a fixed-width field, and "0x05" lines up in a report against
            // the other bytes of the same frame where "5 (0x5)" does not. The
            // integral branch's both-bases rendering exists because a register
            // *width* is not knowable there; here it is, exactly one octet.
            //
            return formatByte( value);
        }
        else
        {
            return {};
        }
    }
} // namespace core
