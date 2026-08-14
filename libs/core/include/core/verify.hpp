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
} // namespace core
