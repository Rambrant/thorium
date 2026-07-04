#pragma once

#include <string_view>

#include "core/criterion.hpp"

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
} // namespace core