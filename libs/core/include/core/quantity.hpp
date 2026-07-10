#pragma once

#include <cmath>
#include <compare>
#include <concepts>
#include <type_traits>

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

            //
            // Same-unit difference, kept in-unit (a Voltage minus a Voltage is
            // still a Voltage). This is what lets predicates.hpp express an
            // epsilon tolerance -- |actual - expected| <= epsilon -- for
            // Quantity<Unit> the same way it does for plain floating_point.
            //
            friend constexpr auto operator-( Quantity lhs, Quantity rhs) -> Quantity
            {
                return Quantity{ lhs.mValue - rhs.mValue };
            }

            //
            // Unary negation. Needed so the epsilon-safe relational predicates
            // (LT/LE/GT/GE/NE in predicates.hpp) can write `-tolerance` for
            // Quantity<Unit> the same way they do for plain floating_point.
            //
            friend constexpr auto operator-( Quantity q) -> Quantity
            {
                return Quantity{ -q.mValue };
            }

            //
            // Same-unit sum. Needed alongside operator- so RangePredicate's
            // `high + epsilon` compiles for Quantity<Unit> the same way it
            // does for plain floating_point.
            //
            friend constexpr auto operator+( Quantity lhs, Quantity rhs) -> Quantity
            {
                return Quantity{ lhs.mValue + rhs.mValue };
            }

        private:
            double mValue{};
    };

    //
    // is_quantity / QuantityType: lets other headers (predicates.hpp) detect
    // "this is some Quantity<Unit>" without knowing which Unit, so tolerance
    // predicates can be written once for both plain floating_point and any
    // Quantity<Unit>, instead of duplicating epsilon()/operator() per unit.
    //
    template<typename T>
    struct is_quantity : std::false_type {};

    template<typename Unit>
    struct is_quantity<Quantity<Unit>> : std::true_type {};

    template<typename T>
    concept QuantityType = is_quantity<T>::value;

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

    constexpr auto operator*(  const Current lhs, const Voltage rhs) -> ApparentPower
    {
        return ApparentPower{ lhs.value() * rhs.value() };
    }

    //
    // Scalar algebra: a Quantity<Unit> scaled by a bare floating-point factor
    // stays in the same unit -- W * 2.0 is a doubling of the Power, still a
    // Power. Quantity<Unit> * Quantity<Unit> has no such generic meaning and
    // is deliberately never defined here; only the specific cross-unit
    // combination above (Voltage * Current) exists. Because Quantity<Unit>
    // never satisfies std::floating_point, these scalar overloads can never
    // collide with that cross-unit one, and W * W (or any Q * Q) remains a
    // compile error by omission rather than by explicit deletion.
    //
    template< typename Unit, std::floating_point T>
    [[nodiscard]]
    constexpr auto operator*( Quantity<Unit> q, T scalar) -> Quantity<Unit>
    {
        return Quantity<Unit>{ q.value() * static_cast<double>( scalar) };
    }

    template< typename Unit, std::floating_point T>
    [[nodiscard]]
    constexpr auto operator*( T scalar, Quantity<Unit> q) -> Quantity<Unit>
    {
        return q * scalar;
    }

    //
    // Scalar division mirrors scalar multiplication -- W / 2.0 halves the
    // Power, still in Watts. There is no matching "scalar / Quantity"
    // overload: that would need a new inverse unit (e.g. 1/W) that Thorium
    // doesn't model, so it's deliberately not provided. Same-unit division
    // (W / W) is likewise not provided yet -- it would need a dimensionless
    // ratio result, which is a separate design question from this pass.
    //
    template< typename Unit, std::floating_point T>
    [[nodiscard]]
    constexpr auto operator/( Quantity<Unit> q, T scalar) -> Quantity<Unit>
    {
        return Quantity<Unit>{ q.value() / static_cast<double>( scalar) };
    }

    //
    // Magnitude, in-unit. Mirrors std::abs so an unqualified `abs(q)` inside
    // core (predicates.hpp) resolves here for Quantity<Unit> and to std::abs
    // for plain floating_point, via ordinary unqualified lookup.
    //
    template<typename Unit>
    [[nodiscard]]
    constexpr auto abs( Quantity<Unit> q) -> Quantity<Unit>
    {
        return Quantity<Unit>{ std::abs( q.value()) };
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