#pragma once

#include <optional>
#include <string_view>

#include "core/criterion.hpp"
#include "core/format.hpp"
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
                                    describeValue( value), numericOf( value), unitOf<T>(), passed);

        return passed;
    }

    //
    // Convenience overload for ad-hoc checks that don't need a named Criterion
    // constant declared up front via CRIT().
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
                                        "<no reading>", std::nullopt, unitOf<Quantity<Unit>>(), false);

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
} // namespace core
