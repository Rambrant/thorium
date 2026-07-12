#pragma once

#include <optional>
#include <string_view>

#include "core/criterion.hpp"
#include "core/quantity.hpp"

namespace core
{
    namespace detail
    {
        //
        // Non-template, so it can live in verify.cpp rather than being header-only.
        // Logs one criterion's result; the two Verify() overloads below both funnel
        // into this.
        //
        void reportResult( std::string_view group,
                            std::string_view id,
                            std::string_view description,
                            bool             passed );
    } // namespace detail

    //
    // The actual check: evaluate the criterion's predicate against value, log
    // the outcome, and return whether it passed.
    //
    template< typename Predicate, typename T>
        requires core::PredicateFor< Predicate, T>
    bool Verify( const core::Criterion<Predicate> & criterion, const T & value)
    {
        const bool passed = criterion.predicate( value);

        detail::reportResult( criterion.group, criterion.id, criterion.description, passed);

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
        return Verify( core::Criterion{ group, id, description, predicate }, value);
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
    bool Verify( const core::Criterion<Predicate> & criterion, const std::optional<Quantity<Unit>> & reading)
    {
        if( ! reading)
        {
            detail::reportResult( criterion.group, criterion.id, criterion.description, false);

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