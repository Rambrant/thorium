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
    concept HasEpsilon = requires( Pred pred, Eps eps) { pred.epsilon( eps); };

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

    template<typename T>
    concept CanNE = requires( T a) { core::NE( a); };

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
    static_assert( !HasEpsilon<core::EqPredicate<int>,    int> );
    static_assert( !HasEpsilon<core::RangePredicate<int>, int> );
    static_assert( !HasEpsilon<core::RelationalPredicate<int, core::Relation::Lt>, int> );
    static_assert( !HasEpsilon<core::NotPredicate<core::EqPredicate<int>>,         int> );

    //
    // The Adhoc-criterion bug this file guards against: a raw double cannot
    // silently become an epsilon for a Quantity<Unit>. The epsilon must be
    // stated in the same unit as the value it tolerates, e.g. `.epsilon( 0.05_W)`.
    //
    static_assert( !HasEpsilon<core::EqPredicate<core::Power>,                  double> );
    static_assert( !HasEpsilon<core::RangePredicate<core::Voltage>,             double> );
    static_assert( !HasEpsilon<core::RelationalPredicate<core::Power, core::Relation::Lt>, double> );

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
    static_assert(  HasEpsilon<core::EqPredicate<double>,       double> );
    static_assert(  HasEpsilon<core::EqPredicate<core::Power>,  core::Power> );
    static_assert(  HasEpsilon<core::RangePredicate<double>,    double> );
    static_assert(  HasEpsilon<core::RangePredicate<core::Voltage>, core::Voltage> );
    static_assert(  HasEpsilon<core::RelationalPredicate<double,     core::Relation::Lt>, double> );
    static_assert(  HasEpsilon<core::NotPredicate<core::EqPredicate<double>>,       double> );
    static_assert(  HasEpsilon<core::NotPredicate<core::EqPredicate<core::Power>>,  core::Power> );
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