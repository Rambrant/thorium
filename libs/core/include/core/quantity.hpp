#pragma once

#include <compare>
#include <concepts>

namespace core
{
    //
    // Unit tags. These exist only to make Quantity<Tag> a distinct type per unit,
    // so e.g. a Voltage can never be silently compared against a Current.
    //
    struct V_Tag {};
    struct A_Tag {};
    struct VA_Tag {};
    struct W_Tag {};
    struct Ohm_Tag {};
    struct dB_Tag {};
    struct Hz_Tag {};
    struct time_Tag {};

    template< typename Unit>
    class Quantity
    {
        public:
            using unitType = Unit;

            constexpr Quantity() = default;

            template< std::floating_point T>
            constexpr explicit Quantity( T value) : mValue( static_cast<double>( value)) {}

            [[nodiscard]]
            constexpr auto value() const -> double
            {
                return mValue;
            }

            //
            // Defining both == and <=> (rather than just <=>) gives the compiler
            // everything it needs to synthesize ==, !=, <, <=, >, >= via C++20's
            // rewritten-candidate rules. Comparisons only compile between the same
            // Unit, so mixing e.g. Voltage and Current is a compile error.
            //
            friend constexpr auto operator==( Quantity lhs, Quantity rhs) -> bool
            {
                return lhs.mValue == rhs.mValue;
            }

            friend constexpr auto operator<=>( Quantity lhs, Quantity rhs) -> std::partial_ordering
            {
                return lhs.mValue <=> rhs.mValue;
            }

        private:
            double mValue{};
    };

    using Voltage       = Quantity< V_Tag>;
    using Current       = Quantity< A_Tag>;
    using Power         = Quantity< W_Tag>;
    using ApparentPower = Quantity< VA_Tag>;
    using Resistance    = Quantity< Ohm_Tag>;
    using Time          = Quantity< time_Tag>;
    using Decibel       = Quantity< dB_Tag>;
    using Frequency     = Quantity< Hz_Tag>;

    //
    // Algebra: combining distinct units to produce a new unit.
    //
    constexpr auto operator*( const Voltage lhs, const Current rhs ) -> ApparentPower
    {
        return ApparentPower{ lhs.value() * rhs.value() };
    }

    //
    // Literals live in their own namespace (mirrors std::chrono::literals) so
    // pulling them in requires an explicit `using namespace core::literals;`
    // rather than polluting every translation unit that includes this header.
    //
    namespace literals
    {
        constexpr Voltage operator""_V( long double v ) { return Voltage{ static_cast<double>(v) }; }
        constexpr Voltage operator""_kV( long double v ) { return Voltage{ static_cast<double>(v * 1000.0) }; }
        constexpr Voltage operator""_mV( long double v ) { return Voltage{ static_cast<double>(v / 1000.0) }; }

        constexpr Current operator""_A( long double v ) { return Current{ static_cast<double>(v) }; }
        constexpr Current operator""_mA( long double v ) { return Current{ static_cast<double>(v / 1000.0) }; }

        constexpr Power operator""_W( long double v ) { return Power{ static_cast<double>(v) }; }
        constexpr Power operator""_kW( long double v ) { return Power{ static_cast<double>(v * 1000.0) }; }
        constexpr Power operator""_mW( long double v ) { return Power{ static_cast<double>(v / 1000.0) }; }

        constexpr ApparentPower operator""_VA( long double v ) { return ApparentPower{ static_cast<double>(v) }; }
        constexpr ApparentPower operator""_mVA( long double v ) { return ApparentPower{ static_cast<double>(v / 1000.0) }; }

        constexpr Time operator""_s( long double v ) { return Time{ static_cast<double>(v) }; }
        constexpr Time operator""_ms( long double v ) { return Time{ static_cast<double>(v / 1000.0) }; }

        constexpr Resistance operator""_Ohm( long double v ) { return Resistance{ static_cast<double>(v) }; }
        constexpr Resistance operator""_kOhm( long double v ) { return Resistance{ static_cast<double>(v) * 1000.0 }; }

        constexpr Frequency operator""_Hz( long double v ) { return Frequency{ static_cast<double>(v) }; }
        constexpr Frequency operator""_kHz( long double v ) { return Frequency{ static_cast<double>(v) * 1000.0 }; }
    } // namespace literals
}     // namespace core
