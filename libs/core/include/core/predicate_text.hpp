#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/format.hpp"
#include "core/predicates.hpp"

//
// Spelling a criterion's own tolerance out in words, so a log line can say what
// was *required* next to what was measured.
//
// Without this, a log can only report a criterion's description -- the prose its
// author happened to write in the CRIT entry ("5Vdc port within +/-50mV"). That
// is traceability text, not the tolerance: nothing checks it against the
// predicate beside it, so it can be vague, or stale, or quietly disagree with
// the number actually being enforced. Rendering the predicate itself means the
// log states the limit the run really applied.
//
// Deliberately not in core/predicates.hpp, even though these overloads describe
// exactly the types declared there. That file is the predicate *logic* --
// constexpr, evaluated at compile time, included by every criteria table in the
// build -- and how a human wants a tolerance spelled is not part of it. Same
// split, and the same reasoning, as core/format.hpp being separate from
// core/quantity_kind.hpp.
//
// Free functions rather than a describe() member on each predicate, for the same
// reason and one more: a predicate can be any callable at all (see
// core::PredicateFor), including a lambda or a hand-written struct in a rig's
// own code. A member would be a requirement those can't meet; an overload set
// degrades instead -- see describeCriterion() at the bottom, which yields an
// empty string for anything it has no overload for rather than failing to
// compile.
//
namespace core::quantities
{
    namespace detail
    {
        //
        // " +/-0.05 V", or nothing at all when the tolerance is the
        // machine-epsilon default (see defaultEpsilon in core/predicates.hpp).
        // That default exists to absorb representation error, not to express a
        // physical margin, so reporting it would put "+/-2.22045e-16 V" in a
        // test report and imply somebody chose it.
        //
        // ASCII "+/-" rather than U+00B1: it matches how the existing criteria
        // descriptions already write it (see dut/criteria_production.inc), and
        // it needs no escaping in any of the three streams this text reaches.
        //
        template<typename T>
        auto toleranceText( const T & tolerance) -> std::string
        {
            if constexpr( Toleranced<T>)
            {
                if( tolerance == defaultEpsilon<T>())
                {
                    return {};
                }

                return " +/-" + core::describeValue( tolerance);
            }
            else
            {
                return {};
            }
        }

        //
        // Bare hex for a mask/expect pattern -- core::describeValue gives an
        // integral in both bases ("245 (0xF5)"), which is right for a *reading*
        // but wrong inside a bit-pattern expression, where the decimal is noise
        // and the hex is the whole point.
        //
        template<std::integral T>
        auto patternText( const T value) -> std::string
        {
            return core::formatHex( static_cast<std::uint64_t>( static_cast<std::make_unsigned_t<T>>( value)));
        }
    } // namespace detail

    template<typename T>
    [[nodiscard]]
    auto describePredicate( const EqPredicate<T> & predicate) -> std::string
    {
        return "= " + core::describeValue( predicate.expected) + detail::toleranceText( predicate.tolerance);
    }

    template<typename T>
    [[nodiscard]]
    auto describePredicate( const RangePredicate<T> & predicate) -> std::string
    {
        return "in [" + core::describeValue( predicate.low) + " .. " + core::describeValue( predicate.high) + "]"
             + detail::toleranceText( predicate.tolerance);
    }

    template<typename T, Relation R>
    [[nodiscard]]
    auto describePredicate( const RelationalPredicate<T, R> & predicate) -> std::string
    {
        //
        // The relation is a template parameter, so the symbol is chosen at
        // compile time -- no runtime switch, and a Relation added to the enum
        // without a symbol here is a compile error in this function rather than
        // a log line reading "? 5 V".
        //
        constexpr std::string_view symbol =
            ( R == Relation::Lt) ? "<"  :
            ( R == Relation::Le) ? "<=" :
            ( R == Relation::Gt) ? ">"  : ">=";

        return std::string( symbol) + " " + core::describeValue( predicate.expected) + detail::toleranceText( predicate.tolerance);
    }

    template<std::integral T>
    [[nodiscard]]
    auto describePredicate( const MaskPredicate<T> & predicate) -> std::string
    {
        return "(value & " + detail::patternText( predicate.mask) + ") == " + detail::patternText( predicate.expected);
    }

    template<typename T, std::size_t N>
    [[nodiscard]]
    auto describePredicate( const AnyPredicate<T, N> & predicate) -> std::string
    {
        std::string result = "one of [";

        for( std::size_t i = 0; i < N; ++i)
        {
            result += ( i > 0 ? ", " : "") + core::describeValue( predicate.options[ i]);
        }

        return result + "]";
    }

    //
    // NE( x) is NotPredicate{ EQ( x)} (see core/predicates.hpp on why it isn't
    // its own formula), and "!= 5 V" is what a reader expects to see for it --
    // so it gets its own overload rather than falling through to the generic
    // negation below, which would render it as "not (= 5 V)".
    //
    template<typename T>
    [[nodiscard]]
    auto describePredicate( const NotPredicate<EqPredicate<T>> & predicate) -> std::string
    {
        return "!= " + core::describeValue( predicate.predicate.expected)
                     + detail::toleranceText( predicate.predicate.tolerance);
    }

    //
    // Any other negation, provided the thing being negated can describe itself.
    // Constrained rather than unconditional: a NotPredicate wrapping a lambda
    // has nothing to say, and saying "not ()" would be worse than saying
    // nothing -- see describeCriterion() below on the empty-string contract.
    //
    // Declared after every overload above so those are all visible to this one's
    // own requires-clause.
    //
    template<typename Pred>
        requires requires( const Pred & inner) { describePredicate( inner); }
    [[nodiscard]]
    auto describePredicate( const NotPredicate<Pred> & predicate) -> std::string
    {
        return "not (" + describePredicate( predicate.predicate) + ")";
    }
} // namespace core::quantities

namespace core
{
    //
    // Whether a predicate has a describePredicate overload reachable by ADL.
    // True for everything in core::quantities' vocabulary; false for a lambda,
    // a std::function, or a rig's own predicate struct that hasn't written one.
    //
    template<typename P>
    concept DescribablePredicate = requires( const P & predicate)
    {
        { describePredicate( predicate) } -> std::convertible_to<std::string>;
    };

    //
    // The criterion's tolerance as log text, or an empty string when the
    // predicate can't describe itself.
    //
    // Empty rather than a compile error, deliberately: core::Verify accepts any
    // callable (see core::PredicateFor), and a rig that checks something with a
    // one-off lambda must not be unable to *run* merely because that lambda
    // can't be printed. The log then shows the criterion's description and its
    // measured value, exactly as it did before this existed -- a log line with
    // less in it, not a build that fails.
    //
    template<typename P>
    [[nodiscard]]
    auto describeCriterion( const P & predicate) -> std::string
    {
        if constexpr( DescribablePredicate<P>)
        {
            return describePredicate( predicate);
        }
        else
        {
            return {};
        }
    }
} // namespace core
