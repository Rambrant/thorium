#pragma once

#include <cmath>
#include <compare>
#include <concepts>
#include <type_traits>

namespace core
{
    //
    // Quantities (unit-tagged values, and -- see predicates.hpp, which
    // reopens this same namespace -- the predicate vocabulary that checks
    // them) live in their own namespace, the same idea as core::literals
    // below: a criteria file (or anything else) that wants exactly this
    // vocabulary can `using namespace core::quantities;` without also
    // pulling in Instrument, Verify, SwitchMatrix, Logger, and everything
    // else in core. Anything new added here (a new unit, a new predicate)
    // is automatically part of that vocabulary -- there's no separate
    // re-export list to remember to keep in sync.
    //
    // Deliberately NOT re-exported into core:: itself (no
    // `using namespace quantities;` here) -- that would silently
    // reintroduce the exact coupling this split exists to avoid: anyone
    // writing `using namespace core;` elsewhere would still pull in EQ,
    // Voltage, etc. Every caller in the codebase either qualifies with
    // core::quantities:: or brings in exactly what it needs.
    //
    namespace quantities
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
        struct PF_Tag {};
        struct var_Tag {};

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
        using PowerFactor   = Quantity< PF_Tag>;
        using ReactivePower = Quantity< var_Tag>;

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
        // Real power (W) vs. apparent power (VA): P = S * PF, where PF is the
        // dimensionless power factor cos(phi). PowerFactor is still its own
        // Quantity<Unit> (not a bare double) so a raw 0.95 can't silently stand
        // in for it -- same reasoning as every other unit here, even though this
        // one has no physical dimension. No literal suffix is provided for it
        // (unlike _V, _A, _W, ...): a bare number has no unit to abbreviate, so
        // `PowerFactor{ 0.95}` is the constructor call, not a "0.95_pf" literal.
        //
        // PowerFactor's valid range ([-1, 1]) is deliberately not enforced here;
        // like every other Quantity<Unit>, it's a plain value holder, and range
        // checking is a CRIT/predicate's job, not the type's.
        //
        constexpr auto operator*( const ApparentPower s, const PowerFactor pf ) -> Power
        {
            return Power{ s.value() * pf.value() };
        }

        constexpr auto operator*( const PowerFactor pf, const ApparentPower s ) -> Power
        {
            return Power{ pf.value() * s.value() };
        }

        //
        // The two algebraic inverses of P = S * PF: divide out the factor to
        // recover apparent power, or divide out the apparent power to recover
        // the factor. Both are ordinary double division underneath -- no
        // divide-by-zero guard, same as every other Quantity division here.
        //
        constexpr auto operator/( const Power p, const ApparentPower s ) -> PowerFactor
        {
            return PowerFactor{ p.value() / s.value() };
        }

        constexpr auto operator/( const Power p, const PowerFactor pf ) -> ApparentPower
        {
            return ApparentPower{ p.value() / pf.value() };
        }

        //
        // The rest of the power triangle: S^2 = P^2 + Q^2, connecting apparent,
        // real, and reactive power. This is deliberately NOT expressed as
        // operator+/operator- (e.g. "s - p" to mean Q), because that would look
        // like ordinary scalar arithmetic while actually being wrong -- the
        // three legs combine as a Pythagorean triple, not additively.
        // ApparentPower - Power already doesn't compile today (different
        // units), and that's left alone rather than repurposed to mean this.
        // Given any two legs, the third is one named function away.
        //
        // No physical-validity check is performed (e.g. S >= P always holds for
        // a real triangle) -- like every other Quantity<Unit> here, this is a
        // plain value holder. Inconsistent inputs (measurement noise, etc.)
        // produce a NaN, which a downstream Verify/predicate simply fails
        // against, rather than needing a guard in core.
        //
        [[nodiscard]]
        constexpr auto reactivePower( const ApparentPower s, const Power p ) -> ReactivePower
        {
            return ReactivePower{ std::sqrt( s.value() * s.value() - p.value() * p.value() ) };
        }

        [[nodiscard]]
        constexpr auto realPower( const ApparentPower s, const ReactivePower q ) -> Power
        {
            return Power{ std::sqrt( s.value() * s.value() - q.value() * q.value() ) };
        }

        [[nodiscard]]
        constexpr auto apparentPower( const Power p, const ReactivePower q ) -> ApparentPower
        {
            return ApparentPower{ std::sqrt( p.value() * p.value() + q.value() * q.value() ) };
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
    } // namespace quantities

    //
    // Literals live in their own namespace (mirrors std::chrono::literals) so
    // pulling them in requires an explicit `using namespace core::literals;`
    // rather than polluting every translation unit that includes this header.
    // A sibling of quantities, not nested inside it -- literal operators
    // reference quantities::Voltage etc. by qualifying with just the sibling
    // namespace name (found via ordinary lookup, no using-directive needed),
    // so this namespace doesn't need any of quantities' own vocabulary
    // (EQ, MASK, ...) in scope, just its types.
    //
    namespace literals
    {
        constexpr quantities::Voltage operator""_V( long double v ) { return quantities::Voltage{ static_cast<double>(v) }; }
        constexpr quantities::Voltage operator""_kV( long double v ) { return quantities::Voltage{ static_cast<double>(v * 1000.0) }; }
        constexpr quantities::Voltage operator""_mV( long double v ) { return quantities::Voltage{ static_cast<double>(v / 1000.0) }; }

        constexpr quantities::Current operator""_A( long double v ) { return quantities::Current{ static_cast<double>(v) }; }
        constexpr quantities::Current operator""_mA( long double v ) { return quantities::Current{ static_cast<double>(v / 1000.0) }; }

        constexpr quantities::Power operator""_W( long double v ) { return quantities::Power{ static_cast<double>(v) }; }
        constexpr quantities::Power operator""_kW( long double v ) { return quantities::Power{ static_cast<double>(v * 1000.0) }; }
        constexpr quantities::Power operator""_mW( long double v ) { return quantities::Power{ static_cast<double>(v / 1000.0) }; }

        constexpr quantities::ApparentPower operator""_VA( long double v ) { return quantities::ApparentPower{ static_cast<double>(v) }; }
        constexpr quantities::ApparentPower operator""_mVA( long double v ) { return quantities::ApparentPower{ static_cast<double>(v / 1000.0) }; }

        //
        // Lowercase "var" (not VAR/VAr) is the correct IEC/SI symbol for
        // reactive power -- see IEC 80000-6 / the EU metric directive.
        // "VAR" is common in the power industry but is not the standard.
        //
        constexpr quantities::ReactivePower operator""_var( long double v ) { return quantities::ReactivePower{ static_cast<double>(v) }; }
        constexpr quantities::ReactivePower operator""_kvar( long double v ) { return quantities::ReactivePower{ static_cast<double>(v * 1000.0) }; }

        constexpr quantities::Time operator""_s( long double v ) { return quantities::Time{ static_cast<double>(v) }; }
        constexpr quantities::Time operator""_ms( long double v ) { return quantities::Time{ static_cast<double>(v / 1000.0) }; }

        constexpr quantities::Resistance operator""_Ohm( long double v ) { return quantities::Resistance{ static_cast<double>(v) }; }
        constexpr quantities::Resistance operator""_kOhm( long double v ) { return quantities::Resistance{ static_cast<double>(v) * 1000.0 }; }

        constexpr quantities::Frequency operator""_Hz( long double v ) { return quantities::Frequency{ static_cast<double>(v) }; }
        constexpr quantities::Frequency operator""_kHz( long double v ) { return quantities::Frequency{ static_cast<double>(v) * 1000.0 }; }
    } // namespace literals
}     // namespace core
