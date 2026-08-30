#include "core/quantities/format.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string_view>
#include <utility>

namespace core
{
    namespace
    {
        //
        // One number, to a chosen count of significant digits. Both public
        // formatters below are this with a different count, so the snprintf
        // details -- the buffer size, the "%g" that drops trailing zeros so an
        // exact 5 stays "5", the guard on a negative return -- are written
        // once.
        //
        // snprintf rather than an ostringstream: this runs once per logged
        // value on both streams, and it needs no locale, no stream state to
        // save and restore, and no allocation beyond the result itself. The
        // buffer is sized for the longest "%.*g" can produce at the widest
        // count used here (sign, digits, point, "e-308") with room to spare.
        //
        [[nodiscard]]
        auto formatDigits( const double value, const int digits) -> std::string
        {
            std::array<char, 32> buffer{};

            const auto written = std::snprintf( buffer.data(), buffer.size(), "%.*g", digits, value);

            return ( written > 0) ? std::string( buffer.data(), static_cast<std::size_t>( written)) : std::string{};
        }

        //
        // The prefix letters, against the exponents they stand for. ASCII "u"
        // rather than the Greek mu, for the reason the _us literal is spelled
        // that way in core/quantities/quantity.hpp: these end up in an RTF
        // report, a SARIF record and a terminal, and a character that renders
        // differently depending on what read the file back is worse than one
        // that is merely plain.
        //
        // The span covers every exponent any unit tag declares. An exponent
        // with no letter here is not an error -- prefixNumber falls back to the
        // plain rendering, the same answer it gives for a value outside its
        // unit's declared span.
        //
        struct PrefixLetter
        {
            int              Exponent;
            std::string_view Letter;
        };

        constexpr std::array<PrefixLetter, 8> kPrefixLetters
        {{
            { -12, "p" }, {  -9, "n" }, {  -6, "u" }, {  -3, "m" },
            {   0, ""  }, {   3, "k" }, {   6, "M" }, {   9, "G" },
        }};

        [[nodiscard]]
        auto prefixLetter( const int exponent) -> std::optional<std::string_view>
        {
            const auto found = std::ranges::find( kPrefixLetters, exponent, &PrefixLetter::Exponent);

            if( found == kPrefixLetters.end())
            {
                return std::nullopt;
            }

            return found->Letter;
        }

        //
        // Significant digits for a prefixed reading -- see PrefixedNumber in
        // core/quantities/format.hpp for why four rather than formatNumber's
        // six.
        //
        constexpr int kPrefixedDigits = 4;

        //
        // The band the mantissa has to land in for a prefix to be an
        // improvement, once the exponent has been clamped to what the unit
        // allows.
        //
        // Not [1, 1000): a value just below its unit's smallest prefix reads
        // better inside that prefix than in exponent form ("0.5 uV" over
        // "5e-07 V"), and the same argument stops holding a few decades
        // further down, where "0.0001 uV" is no longer a number anybody can
        // read at a glance. Three decades of headroom below the prefix is
        // where that turns over.
        //
        constexpr double kMantissaFloor   = 0.001;
        constexpr double kMantissaCeiling = 1000.0;
    } // namespace

    auto formatNumber( const double value) -> std::string
    {
        return formatDigits( value, 6);
    }

    auto prefixNumber( const double value, const quantities::SiPrefixRange range) -> PrefixedNumber
    {
        //
        // A zero has no magnitude to take a prefix from, and neither does a
        // NaN or an infinity -- log10 of any of them is not a number this can
        // clamp. "0 V" is also simply what a reader wants; "0 mV" would be a
        // claim about a range that was never measured.
        //
        if( ! std::isfinite( value) || value == 0.0)
        {
            return { formatNumber( value), {} };
        }

        const auto exponent = static_cast<int>( std::floor( std::log10( std::fabs( value))));

        //
        // Floored to a multiple of three, and floored in doubles rather than by
        // integer division: C++ truncates towards zero, so exponent -4 would
        // give group -3 where it has to give -6. The difference is 100 uV
        // rendering as "0.1 mV" instead of "100 uV" -- or, one decade further
        // down, as "0.01 mV".
        //
        auto group = static_cast<int>( std::floor( exponent / 3.0)) * 3;

        group = std::clamp( group, range.Smallest, range.Largest);

        auto mantissa = value / std::pow( 10.0, group);
        auto text     = formatDigits( mantissa, kPrefixedDigits);

        //
        // Rounding can carry the mantissa over the top of its own band: 999.97
        // is "1000" at four digits, which should read "1 kV" rather than
        // "1000 V" -- but only where the unit's span actually reaches that far.
        //
        if( ( text == "1000" || text == "-1000") && ( group + 3) <= range.Largest)
        {
            group   += 3;
            mantissa = value / std::pow( 10.0, group);
            text     = formatDigits( mantissa, kPrefixedDigits);
        }

        const auto letter = prefixLetter( group);

        //
        // Outside what the unit declared -- a picovolt against a span that
        // stops at microvolts -- the clamp has left a mantissa that is no
        // longer a legible number, and dressing it in a prefix letter would
        // read worse than the plain exponent form does. Same answer for an
        // exponent no letter is known for, so a unit tag can never make this
        // print a prefix that does not exist.
        //
        const auto magnitude = std::fabs( mantissa);

        if( ! letter.has_value() || magnitude < kMantissaFloor || magnitude >= kMantissaCeiling)
        {
            return { formatNumber( value), {} };
        }

        return { std::move( text), *letter };
    }

    auto formatHex( const std::uint64_t value) -> std::string
    {
        std::array<char, 32> buffer{};

        const auto written = std::snprintf( buffer.data(), buffer.size(), "0x%llX", static_cast<unsigned long long>( value));

        return ( written > 0) ? std::string( buffer.data(), static_cast<std::size_t>( written)) : std::string{};
    }

    auto formatByte( const std::byte value) -> std::string
    {
        std::array<char, 8> buffer{};

        const auto written = std::snprintf( buffer.data(), buffer.size(), "0x%02X",
                                            static_cast<unsigned>( std::to_integer<unsigned char>( value)));

        return ( written > 0) ? std::string( buffer.data(), static_cast<std::size_t>( written)) : std::string{};
    }
} // namespace core
