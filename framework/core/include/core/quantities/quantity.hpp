#pragma once

#include <cmath>
#include <compare>
#include <concepts>
#include <string_view>
#include <type_traits>

namespace core
{
    //
    // Quantities (unit-tagged values, and -- see predicates.hpp, which
    // reopens this same namespace -- the predicate vocabulary that checks
    // them) live in their own namespace, the same idea as core::literals
    // below: a criteria file (or anything else) that wants exactly this
    // vocabulary can `using namespace core::quantities;` without also
    // pulling in Instrument, Verify, SwitchMatrix, and everything
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
        // Unit types. These exist to make Quantity<Type> a distinct type per
        // unit, so e.g. a Voltage can never be silently compared against a
        // Current -- and to be the one place a unit's printed symbol is
        // written down.
        //
        // Symbol used to live in a switch over QuantityKind in
        // core/src/quantities/format.cpp, which made it the ninth per-unit list in this
        // library that had to be kept in step with the other eight. It belongs
        // here because the unit tag IS the unit: everything else that needs the
        // symbol (a runtime table keyed by QuantityKind, a compile-time lookup
        // from a concrete Quantity) is now derived from these declarations
        // rather than restating them -- see core/quantities/quantity_kind.hpp.
        //
        // Note the mapping is deliberately NOT mechanical from the tag's own
        // name -- time_Type is "s", PF_Type has no symbol at all -- which is
        // exactly why stating it here beats reflecting over the type name.
        //
        // PowerFactor's empty symbol is a real value, not an oversight: it is
        // dimensionless (see the algebra below), and "0.95" with nothing after
        // it is the correct rendering.
        //
        struct V_Type    { static constexpr std::string_view Symbol = "V";   };
        struct A_Type    { static constexpr std::string_view Symbol = "A";   };
        struct VA_Type   { static constexpr std::string_view Symbol = "VA";  };
        struct W_Type    { static constexpr std::string_view Symbol = "W";   };
        struct Ohm_Type  { static constexpr std::string_view Symbol = "Ohm"; };
        struct dB_Type   { static constexpr std::string_view Symbol = "dB";  };
        struct Hz_Type   { static constexpr std::string_view Symbol = "Hz";  };
        struct time_Type { static constexpr std::string_view Symbol = "s";   };
        struct PF_Type   { static constexpr std::string_view Symbol = "";    };
        struct var_Type  { static constexpr std::string_view Symbol = "var"; };

        //
        // Temperature, in two units that are not two scales of one thing.
        //
        // Celsius is where a temperature *is*; kelvin, here, is how far apart
        // two of them are. "85 degC" and "a 20 K rise" are different kinds of
        // statement, and the difference is not stylistic: 85 degC + 20 degC is
        // meaningless, while 85 degC + 20 K is 105 degC, and only one of those
        // should compile. That distinction is what DifferenceType below
        // declares, and it is the only reason K_Type exists -- this framework
        // has no use for an absolute temperature in kelvin, and adding one
        // would just be a second spelling of the first.
        //
        // Celsius is the absolute unit, rather than kelvin, because it is the
        // unit the criteria are written in: a thermal limit is quoted "85 degC"
        // on the datasheets this rig tests against, and a CRIT table that had
        // to be read as 358.15 K would be one conversion away from the spec on
        // every row -- the same argument _mOhm makes for shunts, one scale up.
        //
        // Both symbols are ASCII for exactly the reason Ohm_Type is "Ohm"
        // rather than the omega: a symbol here ends up in an RTF report, a
        // SARIF record and a recording file, and a unit that renders
        // differently depending on what encoding read the file back is worse
        // than one that is merely plain. "degC" is also the SCPI spelling, so
        // an instrument that reports temperature directly agrees with the
        // table without a translation step.
        //
        struct K_Type    { static constexpr std::string_view Symbol = "K";    };
        struct degC_Type { static constexpr std::string_view Symbol = "degC"; using DifferenceType = K_Type; };

        //
        // The unit a difference in this unit is expressed in -- the unit
        // itself, for all but one of the units above.
        //
        // A Voltage minus a Voltage is a Voltage: the scale has a real zero, so
        // a value and a gap between two values are the same kind of thing and
        // there is nothing to distinguish. Celsius has no such zero -- its zero
        // is a convention about water -- so a Celsius value is a *point* and
        // the gap between two of them is not a point at all. That is the whole
        // content of this trait, and it is declared on the unit tag (a nested
        // DifferenceType) rather than in a table here for the reason Symbol is:
        // the tag IS the unit, and a second list would be a second thing to
        // keep in step.
        //
        // Absent means "itself", so every existing unit keeps exactly the
        // arithmetic it always had, and a new ordinary unit needs to say
        // nothing at all.
        //
        namespace detail
        {
            template<typename Unit>
            consteval auto differenceUnit()
            {
                if constexpr( requires { typename Unit::DifferenceType; })
                {
                    return std::type_identity<typename Unit::DifferenceType>{};
                }
                else
                {
                    return std::type_identity<Unit>{};
                }
            }
        } // namespace detail

        template<typename Unit>
        using DifferenceUnit = typename decltype( detail::differenceUnit<Unit>())::type;

        //
        // A unit whose values are points on a scale with no meaningful zero, so
        // that differences live in a unit of their own -- which is exactly what
        // "its DifferenceUnit is not itself" says, so the concept is that
        // sentence rather than a second list naming the units concerned.
        //
        // What it is used for, below: subtraction of two points yields a
        // difference (that much is true of every unit -- see operator-), while
        // *adding* two points, or scaling one, is refused. Twice 20 degC is not
        // 40 degC in any sense a bench cares about, and the same reasoning
        // rules out both. What remains legal is the useful half -- point minus
        // point, point plus or minus a difference -- and the arithmetic on the
        // differences themselves, which are ordinary values with a real zero.
        //
        template<typename Unit>
        concept AffineUnit = ! std::same_as<DifferenceUnit<Unit>, Unit>;

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
                // This quantity's unit symbol, straight from its unit tag. A
                // static member rather than something a formatter looks up:
                // asking a Voltage what it is called needs no table, no
                // QuantityKind round-trip, and no runtime dispatch (see
                // core::unitOf in core/quantities/format.hpp).
                //
                [[nodiscard]]
                static constexpr auto symbol() -> std::string_view
                {
                    return Unit::Symbol;
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
                // Same-unit difference, in that unit's difference unit -- which
                // is the unit itself for all but Celsius, so a Voltage minus a
                // Voltage is still a Voltage and always was. A Temperature minus
                // a Temperature is a TemperatureDelta; see DifferenceUnit above
                // for why that is not an inconsistency but the same rule read
                // twice.
                //
                // predicates.hpp no longer relies on this closing over the type
                // -- it compares on raw values (see detail::asDouble there), so
                // an epsilon tolerance works whatever a difference turns out to
                // be. That is what makes a difference free to be a new type
                // here rather than being forced back into this one.
                //
                friend constexpr auto operator-( Quantity lhs, Quantity rhs) -> Quantity<DifferenceUnit<Unit>>
                {
                    return Quantity<DifferenceUnit<Unit>>{ lhs.mValue - rhs.mValue };
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
                // Same-unit sum -- for every unit whose values are magnitudes,
                // which is all of them but Celsius. Two points on an affine
                // scale have no sum (see AffineUnit above), so the constraint
                // removes this overload rather than letting 20 degC + 20 degC
                // produce a number nothing means.
                //
                // A point plus a *difference* is an ordinary and useful thing to
                // want, and lives with the rest of the algebra below.
                //
                friend constexpr auto operator+( Quantity lhs, Quantity rhs) -> Quantity
                    requires ( ! AffineUnit<Unit>)
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

        using Voltage       = Quantity< V_Type>;
        using Current       = Quantity< A_Type>;
        using Power         = Quantity< W_Type>;
        using ApparentPower = Quantity< VA_Type>;
        using Resistance    = Quantity< Ohm_Type>;
        using Time          = Quantity< time_Type>;
        using Decibel       = Quantity< dB_Type>;
        using Frequency     = Quantity< Hz_Type>;
        using PowerFactor   = Quantity< PF_Type>;
        using ReactivePower = Quantity< var_Type>;

        //
        // Temperature is here as an ordinary unit, and deliberately appears
        // nowhere in the algebra below.
        //
        // A thermistor or an LM35 on the DUT is read by a meter in volts, and
        // the degrees are derived from that reading -- so the temptation is an
        // operator turning a Voltage into a Temperature. Every relation below
        // is physics: Ohm's law and the power triangle hold for every resistor
        // and every load on every bench, which is what earns them a place in
        // the type system. 10 mV/degC is not physics, it is one part number's
        // calibration, and an operator would dress it up as the other kind of
        // fact -- so the wrong sensor's curve would be a silent success rather
        // than a compile error, and there would be nowhere to write down which
        // curve was used.
        //
        // The conversion is a value instead: core::quantities::Transducer
        // (core/quantities/transducer.hpp), named and declared next to the DUT
        // point it belongs to. Its signature is what carries the safety -- a
        // Transducer<Voltage, Temperature> cannot be applied to a Current
        // reading, and cannot be confused with the one written for a different
        // sensor on the same adapter.
        //
        // The pair of them, because a temperature and a temperature difference
        // are different statements -- see degC_Type/K_Type above, and the
        // arithmetic further down. A criterion says "below 85 degC" or "a rise
        // of at most 20 K over ambient", and those are not the same check even
        // when the number is the same.
        //
        using Temperature      = Quantity< degC_Type>;
        using TemperatureDelta = Quantity< K_Type>;

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
        // Ohm's law, all three ways round. This is what lets a shunt reading be
        // converted in units rather than in bare doubles: a shunt is measured as
        // a voltage across a known resistance, so the current through it is
        // V / R -- and writing it that way keeps the result a Current, which a
        // criterion in amps can then check.
        //
        //     const auto drop  = Measure( Dmm1.voltage(), at( dut::ShuntSense));
        //     const auto load  = drop / 10.0_mOhm;      // a Current, not a double
        //
        // Deliberately all three rather than only the one with a caller today:
        // unlike the power triangle below, these are not a choice of formula --
        // they are one relation, and omitting two of its rearrangements would
        // just mean the next caller reaches for .value() and loses the unit.
        //
        constexpr auto operator/( const Voltage v, const Resistance r ) -> Current
        {
            return Current{ v.value() / r.value() };
        }

        constexpr auto operator/( const Voltage v, const Current i ) -> Resistance
        {
            return Resistance{ v.value() / i.value() };
        }

        constexpr auto operator*( const Current i, const Resistance r ) -> Voltage
        {
            return Voltage{ i.value() * r.value() };
        }

        constexpr auto operator*( const Resistance r, const Current i ) -> Voltage
        {
            return Voltage{ r.value() * i.value() };
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
        // Temperature and its difference: the other half of what AffineUnit
        // above set up, and the half a script actually writes.
        //
        // Subtraction is not here, because it needs nothing special -- the
        // generic same-unit operator- already yields a TemperatureDelta, since
        // that is what degC_Type declares its difference unit to be. What has
        // to be written out is the way back: a point offset by a difference is
        // a point again, which no generic rule can know.
        //
        //     const auto rise = hotspot - ambient;        // a TemperatureDelta
        //     const auto ceiling = ambient + 40.0_K;      // a Temperature
        //
        // Both orders of the sum, for the same reason Voltage * Current has
        // both: the operation is commutative and a call site should not have to
        // remember that it is written down only one way round. Delta minus
        // point deliberately has no overload -- "20 K less a temperature" is
        // not a statement about anything.
        //
        constexpr auto operator+( const Temperature t, const TemperatureDelta d) -> Temperature
        {
            return Temperature{ t.value() + d.value() };
        }

        constexpr auto operator+( const TemperatureDelta d, const Temperature t) -> Temperature
        {
            return Temperature{ d.value() + t.value() };
        }

        constexpr auto operator-( const Temperature t, const TemperatureDelta d) -> Temperature
        {
            return Temperature{ t.value() - d.value() };
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
        // Not defined for an affine unit (see AffineUnit above), for the same
        // reason its addition is not: scaling a point on a scale with no real
        // zero answers a question nobody asked -- twice 20 degC is 40 degC only
        // by an accident of where water freezes. A TemperatureDelta scales
        // perfectly well, and that is the value a script has when it wants to
        // halve or double a margin.
        //
        template< typename Unit, std::floating_point T> requires ( ! AffineUnit<Unit>)
        [[nodiscard]]
        constexpr auto operator*( Quantity<Unit> q, T scalar) -> Quantity<Unit>
        {
            return Quantity<Unit>{ q.value() * static_cast<double>( scalar) };
        }

        template< typename Unit, std::floating_point T> requires ( ! AffineUnit<Unit>)
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
        template< typename Unit, std::floating_point T> requires ( ! AffineUnit<Unit>)
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
        //
        // Every unit below is declared twice: once taking long double, for
        // 2.0_V, and once taking unsigned long long, for 2_V. Both are needed
        // because C++ picks the overload from how the literal was *written*,
        // not from what the value is -- with only the first, 2_V is "unable to
        // find numeric literal operator", which is a confusing thing to be told
        // about a voltage that is exactly two volts. Round setpoints are the
        // common case on a bench (28 V, 115 V, 400 Hz), so the form that reads
        // like the datasheet should not be the form that fails to compile.
        //
        // The integer overload delegates to its long double sibling rather than
        // repeating the construction, so each unit's scale factor is written
        // once. Getting 2_kOhm and 2.0_kOhm to disagree by a factor of a
        // thousand is exactly the kind of divergence a copied line invites.
        //
        constexpr quantities::Voltage operator""_V( long double v ) { return quantities::Voltage{ static_cast<double>(v) }; }
        constexpr quantities::Voltage operator""_V( unsigned long long v ) { return operator""_V( static_cast<long double>( v)); }
        constexpr quantities::Voltage operator""_kV( long double v ) { return quantities::Voltage{ static_cast<double>(v * 1000.0) }; }
        constexpr quantities::Voltage operator""_kV( unsigned long long v ) { return operator""_kV( static_cast<long double>( v)); }
        constexpr quantities::Voltage operator""_mV( long double v ) { return quantities::Voltage{ static_cast<double>(v / 1000.0) }; }
        constexpr quantities::Voltage operator""_mV( unsigned long long v ) { return operator""_mV( static_cast<long double>( v)); }

        constexpr quantities::Current operator""_A( long double v ) { return quantities::Current{ static_cast<double>(v) }; }
        constexpr quantities::Current operator""_A( unsigned long long v ) { return operator""_A( static_cast<long double>( v)); }
        constexpr quantities::Current operator""_mA( long double v ) { return quantities::Current{ static_cast<double>(v / 1000.0) }; }
        constexpr quantities::Current operator""_mA( unsigned long long v ) { return operator""_mA( static_cast<long double>( v)); }

        constexpr quantities::Power operator""_W( long double v ) { return quantities::Power{ static_cast<double>(v) }; }
        constexpr quantities::Power operator""_W( unsigned long long v ) { return operator""_W( static_cast<long double>( v)); }
        constexpr quantities::Power operator""_kW( long double v ) { return quantities::Power{ static_cast<double>(v * 1000.0) }; }
        constexpr quantities::Power operator""_kW( unsigned long long v ) { return operator""_kW( static_cast<long double>( v)); }
        constexpr quantities::Power operator""_mW( long double v ) { return quantities::Power{ static_cast<double>(v / 1000.0) }; }
        constexpr quantities::Power operator""_mW( unsigned long long v ) { return operator""_mW( static_cast<long double>( v)); }

        constexpr quantities::ApparentPower operator""_VA( long double v ) { return quantities::ApparentPower{ static_cast<double>(v) }; }
        constexpr quantities::ApparentPower operator""_VA( unsigned long long v ) { return operator""_VA( static_cast<long double>( v)); }
        constexpr quantities::ApparentPower operator""_mVA( long double v ) { return quantities::ApparentPower{ static_cast<double>(v / 1000.0) }; }
        constexpr quantities::ApparentPower operator""_mVA( unsigned long long v ) { return operator""_mVA( static_cast<long double>( v)); }

        //
        // Lowercase "var" (not VAR/VAr) is the correct IEC/SI symbol for
        // reactive power -- see IEC 80000-6 / the EU metric directive.
        // "VAR" is common in the power industry but is not the standard.
        //
        constexpr quantities::ReactivePower operator""_var( long double v ) { return quantities::ReactivePower{ static_cast<double>(v) }; }
        constexpr quantities::ReactivePower operator""_var( unsigned long long v ) { return operator""_var( static_cast<long double>( v)); }
        constexpr quantities::ReactivePower operator""_kvar( long double v ) { return quantities::ReactivePower{ static_cast<double>(v * 1000.0) }; }
        constexpr quantities::ReactivePower operator""_kvar( unsigned long long v ) { return operator""_kvar( static_cast<long double>( v)); }

        constexpr quantities::Time operator""_s( long double v ) { return quantities::Time{ static_cast<double>(v) }; }
        constexpr quantities::Time operator""_s( unsigned long long v ) { return operator""_s( static_cast<long double>( v)); }
        constexpr quantities::Time operator""_ms( long double v ) { return quantities::Time{ static_cast<double>(v / 1000.0) }; }
        constexpr quantities::Time operator""_ms( unsigned long long v ) { return operator""_ms( static_cast<long double>( v)); }

        //
        // Microseconds and nanoseconds, which arrive with the oscilloscope.
        // A scope's timebase and its edge timings live down here -- 5_ns/div
        // is an ordinary setting and a rise time is quoted in nanoseconds --
        // and without these the alternatives were Time{ 12e-9 } or 0.000012_ms,
        // neither of which reads like the number an engineer said out loud.
        //
        // "_us" rather than "_µs": a literal suffix is an identifier, and while
        // C++ would accept the Greek letter, a unit that cannot be typed on
        // every keyboard in the building is a unit that gets avoided. The ASCII
        // spelling is the one every datasheet falls back to for the same
        // reason.
        //
        constexpr quantities::Time operator""_us( long double v ) { return quantities::Time{ static_cast<double>(v / 1'000'000.0) }; }
        constexpr quantities::Time operator""_us( unsigned long long v ) { return operator""_us( static_cast<long double>( v)); }
        constexpr quantities::Time operator""_ns( long double v ) { return quantities::Time{ static_cast<double>(v / 1'000'000'000.0) }; }
        constexpr quantities::Time operator""_ns( unsigned long long v ) { return operator""_ns( static_cast<long double>( v)); }

        constexpr quantities::Resistance operator""_Ohm( long double v ) { return quantities::Resistance{ static_cast<double>(v) }; }
        constexpr quantities::Resistance operator""_Ohm( unsigned long long v ) { return operator""_Ohm( static_cast<long double>( v)); }
        constexpr quantities::Resistance operator""_kOhm( long double v ) { return quantities::Resistance{ static_cast<double>(v) * 1000.0 }; }
        constexpr quantities::Resistance operator""_kOhm( unsigned long long v ) { return operator""_kOhm( static_cast<long double>( v)); }

        //
        // Milliohms, for shunts -- the scale a current-sense resistor is
        // actually specified at, and the one that makes Ohm's law above read
        // like the datasheet rather than like 0.01_Ohm.
        //
        constexpr quantities::Resistance operator""_mOhm( long double v ) { return quantities::Resistance{ static_cast<double>(v) / 1000.0 }; }
        constexpr quantities::Resistance operator""_mOhm( unsigned long long v ) { return operator""_mOhm( static_cast<long double>( v)); }

        constexpr quantities::Frequency operator""_Hz( long double v ) { return quantities::Frequency{ static_cast<double>(v) }; }
        constexpr quantities::Frequency operator""_Hz( unsigned long long v ) { return operator""_Hz( static_cast<long double>( v)); }
        constexpr quantities::Frequency operator""_kHz( long double v ) { return quantities::Frequency{ static_cast<double>(v) * 1000.0 }; }
        constexpr quantities::Frequency operator""_kHz( unsigned long long v ) { return operator""_kHz( static_cast<long double>( v)); }

        //
        // Only the one scale, and no prefixed siblings: a millidegree is not a
        // unit anybody writes a thermal limit in, and a kilodegree is not a
        // temperature this bench will ever see. Negative values are written the
        // ordinary way, -40.0_degC, since a literal is never negative in C++ --
        // the minus is an operator applied to it, which Quantity's unary
        // operator- already provides.
        //
        constexpr quantities::Temperature operator""_degC( long double v ) { return quantities::Temperature{ static_cast<double>(v) }; }
        constexpr quantities::Temperature operator""_degC( unsigned long long v ) { return operator""_degC( static_cast<long double>( v)); }

        //
        // And the difference, in kelvin -- what a rise, a margin or a
        // tolerance-over-ambient is written in. `85_degC` is where the device
        // is; `20_K` is how much hotter it got.
        //
        constexpr quantities::TemperatureDelta operator""_K( long double v ) { return quantities::TemperatureDelta{ static_cast<double>(v) }; }
        constexpr quantities::TemperatureDelta operator""_K( unsigned long long v ) { return operator""_K( static_cast<long double>( v)); }
    } // namespace literals
}     // namespace core
