#include "core/predicates.hpp"
#include "core/quantity.hpp"

#include <gtest/gtest.h>

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

    template<typename Pred, typename Eps>
    concept HasWithin = requires( Pred pred, Eps eps) { pred.within( eps); };

    template<typename A, typename B = A>
    concept EqComparable = requires( A a, B b) { a == b; };

    template<typename A, typename B = A>
    concept Subtractable = requires( A a, B b) { a - b; };

    template<typename Unit, typename Raw>
    concept QuantityConstructibleFrom = requires( Raw r) { core::Quantity<Unit>{ r}; };

    template<typename T>
    concept Maskable = requires( T mask, T expected) { core::MASK( mask, expected); };

    template<typename First, typename... Rest>
    concept Anyable = requires( First first, Rest... rest) { core::ANY( first, rest...); };

    template<typename T>
    concept ThreeWayComparablePredicate = requires( T a, T b) { core::LT( a)( b); };

    //
    // within() is meaningless on an exact type (int, enum, ...) -- there is
    // no such thing as "42 plus or minus 1" for an exact equality check.
    //
    static_assert( !HasWithin<core::EqPredicate<int>,    int> );
    static_assert( !HasWithin<core::RangePredicate<int>, int> );

    //
    // The Adhoc-criterion bug this file guards against: a raw double cannot
    // silently become an epsilon for a Quantity<Unit>. The epsilon must be
    // stated in the same unit as the value it tolerates, e.g. `.within( 0.05_W)`.
    //
    static_assert( !HasWithin<core::EqPredicate<core::Power>,        double> );
    static_assert( !HasWithin<core::RangePredicate<core::Voltage>,   double> );

    //
    // Distinct Quantity<Tag> units are distinct types on purpose -- a
    // Voltage can never be compared against, or combined with, a Current.
    //
    static_assert( !EqComparable<core::Voltage, core::Current> );
    static_assert( !Subtractable<core::Voltage, core::Current> );

    //
    // Quantity's converting constructor is constrained to floating_point,
    // so an int (or any other non-floating-point value) can't construct one.
    //
    static_assert( !QuantityConstructibleFrom<core::V_Tag, int> );

    //
    // MASK is restricted to std::integral -- a float or a Quantity<Unit>
    // has no meaningful bitwise mask operation.
    //
    static_assert( !Maskable<double> );
    static_assert( !Maskable<core::Voltage> );

    //
    // ANY requires every option to be the exact same type as the first --
    // consistent with EQ/IN/etc. never mixing units.
    //
    static_assert( !Anyable<core::Voltage, core::Current> );
    static_assert( !Anyable<double, int> );

    //
    // Mirror positive cases: if any of these ever start failing, one of the
    // negative checks above has been silently satisfied by an unrelated
    // change, rather than the fix actually loosening on purpose.
    //
    static_assert(  HasWithin<core::EqPredicate<double>,       double> );
    static_assert(  HasWithin<core::EqPredicate<core::Power>,  core::Power> );
    static_assert(  EqComparable<core::Voltage> );
    static_assert(  Subtractable<core::Voltage> );
    static_assert(  QuantityConstructibleFrom<core::V_Tag, double> );
    static_assert(  Maskable<unsigned int> );
    static_assert(  Anyable<core::Voltage, core::Voltage, core::Voltage> );
    static_assert(  ThreeWayComparablePredicate<core::Voltage> );
} // namespace

TEST( CoreStaticConstraints, IllegalPredicateAndUnitCombinationsAreCompileErrors)
{
    //
    // Nothing to run: reaching this line at all means every static_assert
    // above already held at compile time. See the file comment.
    //
    SUCCEED();
}