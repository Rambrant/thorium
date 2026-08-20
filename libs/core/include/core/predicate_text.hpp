#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
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
        template<BitPattern T>
        auto patternText( const T value) -> std::string
        {
            return core::formatHex( toBits( value));
        }

        //
        // "[3.3 V, 3.6 V, 5 V]" -- the option list of an ANY, shared with the
        // NONE overload below rather than written out twice. The two differ
        // only in the words in front of the list, and a report where "one of"
        // and "none of" render their options differently would be a defect
        // nothing else would catch.
        //
        template<typename T, std::size_t N>
        auto optionListText( const std::array<T, N> & options) -> std::string
        {
            std::string result = "[";

            for( std::size_t i = 0; i < N; ++i)
            {
                result += ( i > 0 ? ", " : "") + core::describeValue( options[ i]);
            }

            return result + "]";
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

    template<BitPattern T>
    [[nodiscard]]
    auto describePredicate( const MaskPredicate<T> & predicate) -> std::string
    {
        return "(value & " + detail::patternText( predicate.mask) + ") == " + detail::patternText( predicate.expected);
    }

    //
    // Rendered as the sentence the criteria table was written to state, not as
    // the MASK each is equivalent to -- see BITS_SET's own comment in
    // core/predicates.hpp. A report saying "bits 0x0C set" is checkable against
    // a specification by eye; "(value & 0x0C) == 0x0C" makes the reader do the
    // equivalence themselves, every time.
    //
    template<BitPattern T, BitSense Sense>
    [[nodiscard]]
    auto describePredicate( const BitsPredicate<T, Sense> & predicate) -> std::string
    {
        constexpr std::string_view sense = ( Sense == BitSense::Set) ? "set" : "clear";

        return "bits " + detail::patternText( predicate.mask) + " " + std::string( sense);
    }

    //
    // The bit number, not the mask it stands for: BIT_SET<3>() was authored
    // against a specification that numbers the bit, and "bit 3 set" is what
    // traces back to it. Both halves are template parameters, so the whole
    // string is fixed at compile time and a Sense added to the enum without a
    // word here is a compile error in this function rather than a log line
    // reading "bit 3 ?".
    //
    template<unsigned Bit, BitSense Sense>
    [[nodiscard]]
    auto describePredicate( const BitPredicate<Bit, Sense> &) -> std::string
    {
        constexpr std::string_view sense = ( Sense == BitSense::Set) ? "set" : "clear";

        return "bit " + std::to_string( Bit) + " " + std::string( sense);
    }

    template<typename T, std::size_t N>
    [[nodiscard]]
    auto describePredicate( const AnyPredicate<T, N> & predicate) -> std::string
    {
        return "one of " + detail::optionListText( predicate.options)
                         + detail::toleranceText( predicate.tolerance);
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
    // NONE( ...) is NotPredicate{ ANY( ...)} for the reason NE is
    // NotPredicate{ EQ( ...)} (see core/predicates.hpp), and gets its own
    // overload for the same reason NE does: the generic negation below would
    // render it "not (one of [3.3 V, 5 V])", which is a reader having to invert
    // a list in their head against a specification that just says none of them.
    //
    template<typename T, std::size_t N>
    [[nodiscard]]
    auto describePredicate( const NotPredicate<AnyPredicate<T, N>> & predicate) -> std::string
    {
        return "none of " + detail::optionListText( predicate.predicate.options)
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

    //
    // "any of [= 5 V, in [3 V .. 3.6 V]]" -- each member rendered by its own
    // overload, so a nested predicate reads exactly as it would alone and
    // ANY_OF has no opinion about any of them.
    //
    // "any of" rather than ANY's "one of", because the two are different
    // criteria and a report has to be able to say which one ran: "one of" is
    // followed by values, "any of" by conditions.
    //
    // Constrained on every member being describable, and degrading as a whole
    // when one is not -- see describeCriterion() on the empty-string contract.
    // Half a disjunction is not a weaker description of the criterion, it is a
    // different and stricter criterion, and printing it would misreport what
    // the run enforced.
    //
    // Declared last, after every overload above, for the reason the generic
    // negation is: an ANY_OF may hold any of them.
    //
    template<typename... Preds>
        requires ( requires( const Preds & inner) { describePredicate( inner); } && ...)
    [[nodiscard]]
    auto describePredicate( const AnyOfPredicate<Preds...> & predicate) -> std::string
    {
        std::string result = "any of [";
        bool        first  = true;

        std::apply(
            [ &result, &first]( const Preds &... preds)
            {
                (( result += ( first ? "" : ", ") + describePredicate( preds), first = false), ...);
            },
            predicate.predicates);

        return result + "]";
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
