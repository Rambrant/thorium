#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

#include "core/criteria_variants.hpp"
#include "core/criterion.hpp"
#include "core/format.hpp"
#include "core/predicate_text.hpp"
#include "core/quantity.hpp"

namespace core
{
    using quantities::Quantity;

    namespace detail
    {
        //
        // Non-template, so it can live in verify.cpp rather than being header-only.
        // Logs one criterion's result; the three Verify() overloads below all funnel
        // into this.
        //
        // The value that was checked is passed in three renderings -- printable,
        // numeric, and unit -- rather than one, because the two log streams want
        // different ones: the human log prints valueText, the machine log wants
        // the bare number to compare against a limit without re-parsing it (see
        // core/journal.hpp's JournalRecord). All three are produced by
        // core/format.hpp from whatever type T the criterion's predicate
        // accepted, which is why they are strings and an optional<double> here
        // rather than a template parameter reaching into verify.cpp.
        //
        void reportResult( std::string_view      group,
                            std::string_view      id,
                            std::string_view      description,
                            std::string_view      valueText,
                            std::optional<double> numericValue,
                            std::string_view      unit,
                            std::string_view      criterionText,
                            bool                  passed );

        //
        // The same post, for a check that was never made: no value, no number,
        // no unit, and passed = false. Its own function rather than a
        // reportResult call with four empty arguments at each of the Fail
        // overloads below, because the placeholder that stands in for the
        // missing value is a report-layout decision and belongs in exactly one
        // place -- see verify.cpp.
        //
        void reportNotChecked( std::string_view group,
                                std::string_view id,
                                std::string_view description,
                                std::string_view criterionText );
    } // namespace detail

    //
    // The actual check: evaluate the criterion's predicate against value, log
    // the outcome, and return whether it passed.
    //
    template< typename Predicate, typename T>
        requires core::PredicateFor< Predicate, T>
    bool Verify( const Criterion<Predicate> & criterion, const T & value)
    {
        const bool passed = criterion.predicate( value);

        core::detail::reportResult( criterion.group, criterion.id, criterion.description,
                                    describeValue( value), numericOf( value), unitOf<T>(),
                                    describeCriterion( criterion.predicate), passed);

        return passed;
    }

    //
    // Ad-hoc checks: one line of prose, the predicate, the value. No named
    // Criterion declared up front via CRIT(), and -- the point of this overload
    // -- no identifiers invented at the call site to stand in for one.
    //
    // An earlier version took a group and an id as well, because those two are
    // what core::SarifSink pastes into a result's ruleId (see
    // SarifSink::ruleIdFor). That had the causality backwards. A group is a
    // criteria *table's* name and an ad-hoc check belongs to no table; an id is
    // a stable key for tracking one requirement across runs, and an ad-hoc check
    // has no identity to track -- it is an assertion written inline in one
    // script, not a requirement a spec traces to. Asking the author to invent
    // both produced exactly what you would expect: the same two strings copied
    // down a file, every result collapsing under one ruleId, and no complaint
    // from anything.
    //
    // So ad-hoc results are no longer indexed by a criterion at all. They share
    // the "Thorium/Verify" rule, and their prose travels as the result's own
    // subject -- logicalLocations[].name and the human log's subject column --
    // rather than as a key. A check worth tracking individually across runs is
    // a check worth promoting to a CRIT entry, which is where a real id and a
    // real group come from.
    //
    // The prose lands in the Criterion's description slot and the id slot is
    // left empty -- which is also what gives it room to be prose. The human
    // report's subject column is sized for a criterion id ("FS_Supply_1::
    // FS_Supply_5V0"), and a sentence put there overruns it and shoves the
    // value, the limit and the verdict rightwards on that one line; the
    // description is the last thing on the row and has the rest of the line to
    // itself. So an ad-hoc row prints its columns empty where a criterion would
    // have been, and reads across as "this value, against this limit, verdict --
    // and here is what it was". See core/report.cpp's verify row.
    //
    template< typename Predicate, typename T>
        requires core::PredicateFor< Predicate, T>
    bool Verify( std::string_view  description,
                 const Predicate & predicate,
                 const T &         value )
    {
        return Verify( Criterion{ std::string_view{}, std::string_view{}, description, predicate }, value);
    }

    //
    // The same thing with an explicit criteria group and id -- for a check that
    // genuinely does belong to a named table and is meant to be tracked as its
    // own rule, without a CRIT constant to name. Rare: the three-argument form
    // above is the one to reach for by default.
    //
    template< typename Predicate, typename T>
        requires core::PredicateFor< Predicate, T>
    bool Verify( std::string_view  group,
                 std::string_view  id,
                 std::string_view  description,
                 const Predicate & predicate,
                 const T &         value )
    {
        return Verify( Criterion{ group, id, description, predicate }, value);
    }

    //
    // Convenience overload for readings that may be absent (e.g. an unknown
    // test point, or an instrument the rig doesn't have). A missing reading
    // is reported as a failed check rather than silently skipped, so a
    // misconfigured route shows up in the test log instead of vanishing.
    //
    // A criterion against a Quantity<Unit> reading can be written two ways:
    // against the raw double (EQ( 5.0)) or against the Quantity itself
    // (EQ( 5.0_V)). The latter is what actually catches a unit mismatch at
    // compile time -- EQ( 5.0_A) against a Voltage reading fails to
    // compile, where EQ( 5.0) against either "works" the same way, since
    // both unwrap to a bare double. Which one a given criterion uses is
    // picked automatically from the predicate's own type, so callers don't
    // have to know or care.
    //
    template< typename Predicate, typename Unit>
        requires core::PredicateFor< Predicate, double> || core::PredicateFor< Predicate, Quantity<Unit>>
    bool Verify( const Criterion<Predicate> & criterion, const std::optional<Quantity<Unit>> & reading)
    {
        if( ! reading)
        {
            //
            // Logged with an explicit "no reading" in the value column rather
            // than an empty one: a log line showing a failed check with nothing
            // where the measurement should be reads like a formatting bug, when
            // in fact the absent reading IS the finding -- see this overload's
            // own comment above on why a missing reading fails rather than
            // skips. The unit is still reported, since the criterion says what
            // was expected even though nothing arrived; there is no number.
            //
            core::detail::reportResult( criterion.group, criterion.id, criterion.description,
                                        "<no reading>", std::nullopt, unitOf<Quantity<Unit>>(),
                                        describeCriterion( criterion.predicate), false);

            return false;
        }

        if constexpr( core::PredicateFor< Predicate, Quantity<Unit>>)
        {
            return Verify( criterion, *reading);
        }
        else
        {
            return Verify( criterion, reading->value());
        }
    }

    // -----------------------------------------------------------------------
    // Multi-variant criteria
    // -----------------------------------------------------------------------
    //
    // A merged criteria table's CRIT entries are MultiCriterions -- the same
    // criterion under every known tolerance variant at once (see
    // core/criterion.hpp and core/active_criteria.hpp). These overloads are what
    // makes them checkable: pick the variant the run selected, then hand the
    // ordinary single-variant Verify above the Criterion it was always going to
    // get.
    //
    // Everything below the selection is unchanged, which is the point. A
    // variant's criterion reaches reportResult() with its own description and
    // its own rendered tolerance, so a log states the limit that was actually
    // applied rather than the one some other variant would have applied.
    //
    namespace detail
    {
        //
        // The tuple element at a *runtime* index, verified.
        //
        // A fold over a compile-time index pack rather than a switch or an array
        // of function pointers: the variants hold different predicate types, so
        // there is no common signature to switch to, and each arm has to be
        // instantiated separately anyway. The && inside each term is what stops
        // the other arms from evaluating -- exactly one comparison matches, and
        // || short-circuits the rest.
        //
        // matched is not decoration: it is false only if wanted is out of range,
        // which cannot happen through core::selectCriteriaVariant (it validates
        // against the same generated list the tuple was built from) but would
        // otherwise silently return "passed" for a check that never ran.
        //
        template<typename Multi, typename T, std::size_t... I>
        auto verifySelectedVariant( const Multi & multi,
                                    const T &     value,
                                    std::size_t   wanted,
                                    std::index_sequence<I...>) -> bool
        {
            bool result = false;

            const bool matched = ( ( I == wanted && ( result = Verify( std::get<I>( multi.variants), value), true)) || ...);

            return matched && result;
        }

        //
        // Whether T is the std::optional<Quantity<Unit>> a Measure hands back --
        // used only to keep the diagnostic overloads below from competing with
        // the optional overload, which handles an absent reading itself.
        //
        template<typename T>
        constexpr bool isOptionalQuantity = false;

        template<typename Unit>
        constexpr bool isOptionalQuantity<std::optional<Quantity<Unit>>> = true;
    } // namespace detail

    template<typename... Variants, typename T>
        requires ( CriterionFor<Variants, T> && ...)
    bool Verify( const MultiCriterion<Variants...> & criterion, const T & value)
    {
        return detail::verifySelectedVariant( criterion, value, activeCriteriaVariant(),
                                              std::make_index_sequence<sizeof...( Variants)>{});
    }

    //
    // The absent-reading overload, for the same reason the single-variant one
    // above exists -- and it dispatches to that one per variant rather than
    // reimplementing it, so "a missing reading is a failed check, not a skipped
    // one" is stated in exactly one place.
    //
    template<typename... Variants, typename Unit>
        requires ( ( CriterionFor<Variants, double> || CriterionFor<Variants, Quantity<Unit>>) && ...)
    bool Verify( const MultiCriterion<Variants...> & criterion, const std::optional<Quantity<Unit>> & reading)
    {
        return detail::verifySelectedVariant( criterion, reading, activeCriteriaVariant(),
                                              std::make_index_sequence<sizeof...( Variants)>{});
    }

    //
    // --- Diagnostics for a mismatched variant ---
    //
    // Building every variant into the binary means every variant's predicates
    // are now type-checked against the reading each script actually takes, not
    // just the one variant a build happened to select. That is the main thing
    // this mechanism buys (a Current criterion in dut/criteria_aged.inc no
    // longer waits until someone targets aged hardware to be discovered) -- but
    // without these, it buys it as "no matching function for call to Verify"
    // followed by three pages of expanded MultiCriterion template arguments,
    // with nothing saying which variant is the odd one out or why.
    //
    // These are the same overloads with the constraint negated, so one of them
    // is what overload resolution lands on instead, and the error is a sentence.
    // They return bool and are never actually called -- a static_assert has
    // already failed by then.
    //
    template<typename... Variants, typename T>
        requires ( ! detail::isOptionalQuantity<T>) && ( ! ( CriterionFor<Variants, T> && ...))
    bool Verify( const MultiCriterion<Variants...> &, const T &)
    {
        static_assert( meta::detail::dependentFalse<T>,
                       "at least one criteria variant's predicate cannot be checked against this "
                       "reading's type -- every variant of a criterion has to accept the same "
                       "measurement (a Voltage criterion in one variant and a Current one in "
                       "another, say). Check that this CRIT's entry agrees in every "
                       "dut/criteria_*.inc, not just the one this build defaults to.");

        return false;
    }

    template<typename... Variants, typename Unit>
        requires ( ! ( ( CriterionFor<Variants, double> || CriterionFor<Variants, Quantity<Unit>>) && ...))
    bool Verify( const MultiCriterion<Variants...> &, const std::optional<Quantity<Unit>> &)
    {
        static_assert( meta::detail::dependentFalse<Unit>,
                       "at least one criteria variant's predicate cannot be checked against this "
                       "reading's unit -- every variant of a criterion has to accept the same "
                       "measurement. Check that this CRIT's entry agrees in every "
                       "dut/criteria_*.inc, not just the one this build defaults to.");

        return false;
    }

    // -----------------------------------------------------------------------
    // Fail: a check that could not be made
    // -----------------------------------------------------------------------
    //
    // Every Verify above is a statement about a value. This is for the case
    // where there is no value at all -- the capture never triggered, the reply
    // was too short to hold the byte the check was about -- and something still
    // has to reach the report, because a check that simply does not appear
    // reads as a run that did not need it.
    //
    // Before this existed, that was written as a predicate contrived to be
    // false:
    //
    //     Verify( "5Vdc rail dip -- not measured, the capture did not complete",
    //             EQ( true), false);
    //
    // Two call sites had one (suite/scripts/ac_dropout_script.cpp and
    // suite/scripts/console_script.cpp, the latter with a >= 5 against a reply
    // length). Both said in their own comments that what happened is that the
    // check could not be made -- and both then put that contrivance in the
    // report's limit column, which is where a reader looks for what the DUT was
    // required to do.
    //
    // Deliberately not a FAIL *predicate*, which is the other way to spell it.
    // Predicates are the criteria tables' vocabulary -- core::quantities is
    // what dut/criteria_*.inc draws from -- so an always-false one would be
    // legal in a CRIT entry, and a table row that no reading can satisfy is
    // exactly what core/predicates.hpp refuses to let an empty ANY_OF() become
    // ("it compiles, it reads like a check, and it fails the run against a
    // perfectly good DUT"). It would also have to accept every T to be usable
    // with no value, which would make it the one predicate that *satisfies* the
    // variant-mismatch static_asserts above instead of tripping them.
    //
    // Not a new Verb either: a Fail posts as Verb::Verify with Passed = false,
    // so every sink already handles it -- SarifSink's Verify branches give it a
    // real result, core::isHumanRelevant already lets it through, and the human
    // log lines it up in the same columns as the checks around it. A new
    // enumerator would have bought a parallel list in each of those places and
    // nothing else.
    //
    // Returns bool, always false, for the same reason Verify returns one: a
    // script folds it into its verdict (allPassed &= Fail( ...)), and a void
    // Fail would force that line to be written as two. Not [[nodiscard]],
    // matching Verify -- a dropped verdict is the same mistake in both, and
    // singling this one out would suggest it is a different one.
    //

    //
    // The ad-hoc form: prose, and nothing else. For a check that has no named
    // criterion to be about, or -- as in ac_dropout_script -- one whose named
    // criterion would be the wrong thing to record, because the criterion is a
    // claim about how far a rail dipped and nothing here measured that.
    //
    auto Fail( std::string_view reason) -> bool;

    //
    // The named form: this criterion, specifically, could not be checked. For
    // when the criterion's identity is worth keeping in the report -- a
    // consumer tracking FS_Console_Ready across runs wants to see that this run
    // could not answer it, rather than an unattributed line saying something
    // nearby went wrong.
    //
    // The reason takes the description slot, displacing the criterion's own
    // prose. That is the trade, and it is deliberate: the description is the
    // one column a sentence fits in (see the three-argument Verify above on why
    // prose cannot go in the subject column), and the reason is the only thing
    // this event knows that the criteria table does not already say. The
    // tolerance is still rendered, so the row still states what was required --
    // what is missing from it is only the value that never arrived.
    //
    // No unit, unlike the absent-reading Verify above: that one knows Unit from
    // the optional<Quantity<Unit>> it was handed, and this one has no reading
    // type to take one from.
    //
    template<typename Predicate>
    auto Fail( const Criterion<Predicate> & criterion, const std::string_view reason) -> bool
    {
        detail::reportNotChecked( criterion.group, criterion.id, reason,
                                  describeCriterion( criterion.predicate));

        return false;
    }

    namespace detail
    {
        //
        // The selected variant's Criterion, failed -- the same fold, and the
        // same reason for it, as verifySelectedVariant above: the variants hold
        // different predicate types, so there is no common signature to switch
        // on and each arm is its own instantiation anyway.
        //
        // Only the predicate's *text* differs between variants here (group and
        // id are lifted out of the MultiCriterion, and the description slot
        // carries the reason rather than any variant's prose), but it differs
        // for the same reason a verdict would: a log has to quote the tolerance
        // that was actually in force.
        //
        template<typename Multi, std::size_t... I>
        auto failSelectedVariant( const Multi &          multi,
                                  const std::string_view reason,
                                  const std::size_t      wanted,
                                  std::index_sequence<I...>) -> bool
        {
            return ( ( I == wanted && ( Fail( std::get<I>( multi.variants), reason), true)) || ...);
        }
    } // namespace detail

    //
    // What a CRIT in a merged table actually is (see core/criterion.hpp), so
    // this is the overload a script reaches -- FS_Console_1::FS_Console_Ready
    // is a MultiCriterion, not a Criterion.
    //
    template<typename... Variants>
    auto Fail( const MultiCriterion<Variants...> & criterion, const std::string_view reason) -> bool
    {
        const bool matched = detail::failSelectedVariant(
            criterion, reason, activeCriteriaVariant(),
            std::make_index_sequence<sizeof...( Variants)>{});

        if( ! matched)
        {
            //
            // Out of range, which cannot happen through selectCriteriaVariant
            // (it validates against the same generated list these tuples are
            // built from) -- but the failure mode if it ever did is that
            // nothing at all reaches the log, which is the one outcome this
            // verb exists to prevent. The criterion still names itself; only
            // the tolerance is missing, because there was no variant to take
            // one from.
            //
            detail::reportNotChecked( criterion.group, criterion.id, reason, {});
        }

        return false;
    }
} // namespace core
