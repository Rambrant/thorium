#include "core/criteria/predicates.hpp"
#include "core/quantities/quantity.hpp"

#include <gtest/gtest.h>

#include <type_traits>

//
// This file has no runtime assertions -- the checks below are all
// static_assert. If any of them fired, the file (and the whole test
// binary) would fail to *compile*, which is exactly the point: these are
// compile-time-error cases, and a compile-time-error case is proven by
// making the compiler refuse to build the alternative, not by running
// something and checking a bool.
//
// IMPORTANT: express each check as `concept X = requires( Args... a) {...};`
// then `static_assert( !X<Types...> );`, NOT as a bare
// `static_assert( !requires { concreteExpression; } );`. The two look
// equivalent but are not: a requires-expression evaluated directly against
// concrete literals/calls is not reliably SFINAE'd away by either GCC or
// Clang when the failure is an unsatisfied constraint or a failed implicit
// conversion -- both compilers emit it as a hard compile error instead of
// quietly making the requires-expression false. Routing the same check
// through a concept template with its own bound parameters *is* reliably
// soft-failed on both. Verified directly against GCC 13/C++23 and Clang
// 18/C++23 in the sandbox before relying on this pattern.
//
namespace
{
    using namespace core::literals;
    using namespace core::quantities;

    template<typename Pred, typename Eps>
    concept HasEpsilon = requires( Pred pred, Eps eps) { pred.epsilon( eps); };

    template<typename A, typename B = A>
    concept EqComparable = requires( A a, B b) { a == b; };

    template<typename A, typename B = A>
    concept Subtractable = requires( A a, B b) { a - b; };

    template<typename A, typename B = A>
    concept Addable = requires( A a, B b) { a + b; };

    template<typename A, typename B = A>
    concept Scalable = requires( A a, B b) { a * b; };

    template<typename A, typename B = A>
    concept Multipliable = requires( A a, B b) { a * b; };

    template<typename A, typename B = A>
    concept Divisible = requires( A a, B b) { a / b; };

    template<typename Unit, typename Raw>
    concept QuantityConstructibleFrom = requires( Raw r) { Quantity<Unit>{ r}; };

    template<typename T>
    concept Maskable = requires( T mask, T expected) { MASK( mask, expected); };

    template<typename First, typename... Rest>
    concept Anyable = requires( First first, Rest... rest) { ANY( first, rest...); };

    template<typename First, typename... Rest>
    concept Noneable = requires( First first, Rest... rest) { NONE( first, rest...); };

    template<typename... Preds>
    concept AnyOfable = requires( Preds... preds) { ANY_OF( preds...); };

    //
    // Whether a predicate can be *asked about* a value at all -- which is where
    // ANY_OF's unit safety lives, since it cannot be checked when the ANY_OF is
    // built. See ANY_OF's own comment in core/criteria/predicates.hpp.
    //
    template<typename Pred, typename T>
    concept CallableWithValue = requires( const Pred & pred, const T & value) { pred( value); };

    template<typename T>
    concept ThreeWayComparablePredicate = requires( T a, T b) { LT( a)( b); };

    template<typename T>
    concept CanNE = requires( T a) { NE( a); };

    template<typename A, typename B>
    concept CanComputeReactivePower = requires( A a, B b) { reactivePower( a, b); };

    //
    // A type with equality but deliberately no ordering at all. Proves NE's
    // constraint really is only "whatever EQ needs" (currently: nothing at
    // construction, == when actually called) -- LT/GT genuinely cannot be
    // expressed for this type, but NE, being !EQ, works exactly like EQ does.
    //
    struct EqOnly
    {
        int value;
        constexpr bool operator==( const EqOnly & o) const { return value == o.value; }
    };

    //
    // epsilon() is meaningless on an exact type (int, enum, ...) -- there is
    // no such thing as "42 plus or minus 1" for an exact equality check.
    // One concept covers EQ, IN, and the relational family, since they all
    // share the same `.epsilon(...)` name now. NE checks the same thing
    // through NotPredicate<EqPredicate<T>>, since NE( x) is NotPredicate{ EQ( x)}.
    //
    static_assert( !HasEpsilon<EqPredicate<int>,    int> );
    static_assert( !HasEpsilon<RangePredicate<int>, int> );
    static_assert( !HasEpsilon<RelationalPredicate<int, Relation::Lt>, int> );
    static_assert( !HasEpsilon<NotPredicate<EqPredicate<int>>,         int> );
    static_assert( !HasEpsilon<AnyPredicate<int, 2>,                   int> );

    //
    // The Adhoc-criterion bug this file guards against: a raw double cannot
    // silently become an epsilon for a Quantity<Unit>. The epsilon must be
    // stated in the same unit as the value it tolerates, e.g. `.epsilon( 0.05_W)`.
    //
    static_assert( !HasEpsilon<EqPredicate<Power>,                  double> );
    static_assert( !HasEpsilon<RangePredicate<Voltage>,             double> );
    static_assert( !HasEpsilon<RelationalPredicate<Power, Relation::Lt>, double> );
    static_assert( !HasEpsilon<AnyPredicate<Voltage, 3>,            double> );

    //
    // Distinct Quantity<Tag> units are distinct types on purpose -- a
    // Voltage can never be compared against, or combined with, a Current.
    //
    static_assert( !EqComparable<Voltage, Current> );
    static_assert( !Subtractable<Voltage, Current> );

    //
    // Quantity<Unit> * Quantity<Unit> has no generic meaning and is never
    // defined -- only the one specific cross-unit combination (Voltage *
    // Current -> ApparentPower) exists. Multiplying two Powers, two
    // Voltages, or two of anything else is a compile error by omission,
    // the same way it would be for any other pair of units.
    //
    static_assert( !Multipliable<Power,    Power> );
    static_assert( !Multipliable<Voltage,  Voltage> );
    static_assert( !Multipliable<Current,  Current> );

    //
    // Same-unit division (W / W) would need a dimensionless ratio result,
    // which is a separate design question -- not provided yet.
    //
    static_assert( !Divisible<Power, Power> );

    //
    // PowerFactor is a Quantity<Unit> like any other -- it can't be added to
    // or compared against a Power or ApparentPower, and it can't be
    // multiplied by itself (no PF * PF is defined, same reasoning as
    // Power * Power above).
    //
    static_assert( !EqComparable<Power, PowerFactor> );
    static_assert( !Subtractable<Power, PowerFactor> );
    static_assert( !Multipliable<PowerFactor, PowerFactor> );

    //
    // ReactivePower is the third leg of the power triangle, and it's just
    // as distinct a unit as the other two: no cross-unit +/-/* against
    // Power or ApparentPower (the S^2 = P^2 + Q^2 identity is deliberately
    // a named function, not operator overloading -- see quantity.hpp), and
    // no ReactivePower * ReactivePower either.
    //
    static_assert( !EqComparable<Power, ReactivePower> );
    static_assert( !Subtractable<ApparentPower, ReactivePower> );
    static_assert( !Multipliable<ReactivePower, ReactivePower> );

    //
    // Quantity's converting constructor is constrained to floating_point,
    // so an int (or any other non-floating-point value) can't construct one.
    //
    static_assert( !QuantityConstructibleFrom<V_Type, int> );

    //
    // MASK is restricted to std::integral -- a float or a Quantity<Unit>
    // has no meaningful bitwise mask operation.
    //
    static_assert( !Maskable<double> );
    static_assert( !Maskable<Voltage> );

    //
    // ANY requires every option to be the exact same type as the first --
    // consistent with EQ/IN/etc. never mixing units. NONE is !ANY and shares
    // the rule through the one SameOptions concept, so it cannot drift into
    // accepting a list ANY would reject.
    //
    static_assert( !Anyable<Voltage, Current> );
    static_assert( !Anyable<double, int> );
    static_assert( !Noneable<Voltage, Current> );
    static_assert( !Noneable<double, int> );

    //
    // ANY_OF's honest limit, on the record in both directions.
    //
    // A mixed-unit ANY_OF *builds* -- unlike ANY, which rejects it at the
    // factory -- because a predicate need not have a value type to compare
    // against (MASK and BIT_SET<N>() are templates over whatever they are
    // handed). The mistake is still a compile error, just later: the resulting
    // predicate is callable with neither of the units it mixes, so no Verify
    // and no CRIT entry can use it. The positive mirror underneath is what
    // makes that meaningful rather than vacuous -- a single-unit ANY_OF *is*
    // callable.
    //
    static_assert(  AnyOfable<EqPredicate<Voltage>, RangePredicate<Current>> );
    static_assert( !CallableWithValue<AnyOfPredicate<EqPredicate<Voltage>, RangePredicate<Current>>, Voltage> );
    static_assert( !CallableWithValue<AnyOfPredicate<EqPredicate<Voltage>, RangePredicate<Current>>, Current> );
    static_assert(  CallableWithValue<AnyOfPredicate<EqPredicate<Voltage>, RangePredicate<Voltage>>, Voltage> );

    //
    // An empty ANY_OF() would be a criterion no reading can satisfy -- it
    // compiles, it reads like a check, and it fails the run against a good DUT.
    //
    static_assert( !AnyOfable<> );

    //
    // No epsilon() on ANY_OF: its members need not share a value type, so there
    // is no single Eps to take, and each member already carries its own next to
    // the value it tolerates.
    //
    static_assert( !HasEpsilon<AnyOfPredicate<EqPredicate<Voltage>, RangePredicate<Voltage>>, Voltage> );

    //
    // NE only needs equality, not an ordering: LT is impossible for
    // EqOnly (no operator<=>/operator<), but NE works fine, since it's
    // built from EQ rather than sharing RelationalPredicate's Orderable
    // constraint.
    //
    static_assert( !ThreeWayComparablePredicate<EqOnly> );
    static_assert(  CanNE<EqOnly> );

    //
    // Mirror positive cases: if any of these ever start failing, one of the
    // negative checks above has been silently satisfied by an unrelated
    // change, rather than the fix actually loosening on purpose.
    //
    static_assert(  HasEpsilon<EqPredicate<double>,       double> );
    static_assert(  HasEpsilon<EqPredicate<Power>,  Power> );
    static_assert(  HasEpsilon<RangePredicate<double>,    double> );
    static_assert(  HasEpsilon<RangePredicate<Voltage>, Voltage> );
    static_assert(  HasEpsilon<RelationalPredicate<double,     Relation::Lt>, double> );
    static_assert(  HasEpsilon<NotPredicate<EqPredicate<double>>,       double> );
    static_assert(  HasEpsilon<NotPredicate<EqPredicate<Power>>,  Power> );
    static_assert(  HasEpsilon<AnyPredicate<double, 2>,     double> );
    static_assert(  HasEpsilon<AnyPredicate<Voltage, 3>,  Voltage> );
    static_assert(  EqComparable<Voltage> );
    static_assert(  Subtractable<Voltage> );
    static_assert(  QuantityConstructibleFrom<V_Type, double> );
    static_assert(  Maskable<unsigned int> );
    static_assert(  Anyable<Voltage, Voltage, Voltage> );
    static_assert(  Noneable<Voltage, Voltage, Voltage> );
    static_assert(  AnyOfable<EqPredicate<Voltage>> );
    static_assert(  HasEpsilon<NotPredicate<AnyPredicate<Voltage, 2>>, Voltage> );
    static_assert( !HasEpsilon<NotPredicate<AnyPredicate<int, 2>>,         int> );
    static_assert(  ThreeWayComparablePredicate<Voltage> );

    //
    // A Quantity<Unit> scaled by a bare floating-point factor stays in the
    // same unit, in either argument order, and the one true cross-unit
    // multiplication (Voltage * Current) still holds.
    //
    static_assert(  Multipliable<Power,   double> );
    static_assert(  Multipliable<double,        Power> );
    static_assert(  Multipliable<Voltage, Current> );
    static_assert(  Divisible<Power, double> );

    //
    // The real-power / apparent-power / power-factor triangle: P = S * PF
    // holds in either argument order, and both algebraic inverses
    // (recovering PF or S) hold too.
    //
    static_assert(  Multipliable<ApparentPower, PowerFactor> );
    static_assert(  Multipliable<PowerFactor,   ApparentPower> );
    static_assert(  Divisible<Power, ApparentPower> );
    static_assert(  Divisible<Power, PowerFactor> );

    //
    // The reactivePower() named function exists for the one pairing this
    // file bothers to check at compile time (ApparentPower, Power) --
    // realPower() and apparentPower() are exercised at runtime instead in
    // test_quantity.cpp, since all three are just S^2 = P^2 + Q^2 solved
    // for a different leg.
    //
    static_assert(  CanComputeReactivePower<ApparentPower, Power> );

    //
    // Celsius is the one affine unit here: its values are points on a scale
    // with no real zero, so what it may take part in is narrower than for
    // every other unit, and narrower in a way no comment could enforce.
    //
    // What is legal -- the useful half. A point minus a point is a span, a
    // point offset by a span is a point again, and a span is an ordinary
    // magnitude with all the arithmetic that implies.
    //
    static_assert(  Subtractable<Temperature, Temperature> );
    static_assert( std::is_same_v<decltype( Temperature{ 61.5} - Temperature{ 21.5}), TemperatureDelta> );

    static_assert(  Addable<Temperature, TemperatureDelta> );
    static_assert(  Addable<TemperatureDelta, Temperature> );
    static_assert(  Subtractable<Temperature, TemperatureDelta> );

    static_assert(  Addable<TemperatureDelta, TemperatureDelta> );
    static_assert(  Scalable<TemperatureDelta, double> );
    static_assert( std::is_same_v<decltype( TemperatureDelta{ 8.0} - TemperatureDelta{ 12.0}), TemperatureDelta> );

    //
    // What is not. Two temperatures have no sum, and a temperature has no
    // double -- both would be arithmetic on the position of the freezing
    // point of water, which is a fact about the scale rather than about the
    // device. Nor is a span something a temperature can be subtracted from.
    //
    static_assert( !Addable<Temperature, Temperature> );
    static_assert( !Scalable<Temperature, double> );
    static_assert( !Scalable<double, Temperature> );
    static_assert( !Divisible<Temperature, double> );
    static_assert( !Subtractable<TemperatureDelta, Temperature> );

    //
    // And the cross-unit rule is untouched by any of it: degrees and kelvin
    // are as distinct from each other, and from every other unit, as Voltage
    // and Current are.
    //
    static_assert( !EqComparable<Temperature, TemperatureDelta> );
    static_assert( !EqComparable<Temperature, Voltage> );
    static_assert( !Addable<Temperature, Voltage> );

    //
    // The tolerance a Celsius criterion carries is written in degrees, not in
    // kelvin -- see detail::asDouble's comment in predicates.hpp on why the
    // margin is quoted in the unit of the value it qualifies. Since the
    // comparison is done on raw magnitudes, that spelling claims nothing about
    // affine arithmetic; what it does buy is that a stray tolerance in some
    // other unit is still refused.
    //
    static_assert(  HasEpsilon<decltype( EQ( Temperature{ 85.0})), Temperature> );
    static_assert( !HasEpsilon<decltype( EQ( Temperature{ 85.0})), TemperatureDelta> );
    static_assert( !HasEpsilon<decltype( EQ( Temperature{ 85.0})), Voltage> );

    //
    // The predicate family works on an affine unit at all, which is the
    // property the raw-magnitude comparison exists to preserve: before it,
    // every toleranced predicate required `actual - expected` to come back as
    // the same type it started as, and for Temperature it does not.
    //
    static_assert(  CallableWithValue<decltype( LT( Temperature{ 85.0})),  Temperature> );
    static_assert(  CallableWithValue<decltype( IN( Temperature{ 20.0}, Temperature{ 60.0})), Temperature> );
    static_assert( !CallableWithValue<decltype( LT( Temperature{ 85.0})),  TemperatureDelta> );
} // namespace

TEST( CoreStaticConstraints, IllegalPredicateAndUnitCombinationsAreCompileErrors)
{
    //
    // Nothing to run: reaching this line at all means every static_assert
    // above already held at compile time. See the file comment.
    //
    SUCCEED();
}