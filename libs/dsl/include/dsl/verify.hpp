#pragma once

#include <concepts>
#include <string_view>

#include "dsl/criterion.hpp"

namespace dsl
{
    template< typename Predicate, typename Value>
    concept ApplicablePredicate =
        requires( Predicate pred, Value value)
        {
            { pred(value) } -> std::convertible_to< bool>;
        };

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
    // NOTE: this is the overload that was missing in the original -- the other
    // overload below called Verify(Criterion{...}, value) but no such overload
    // existed anywhere, so it couldn't compile or link.
    //
    template< typename Predicate, typename T>
        requires ApplicablePredicate< Predicate, T>
    bool Verify( const Criterion<Predicate> & criterion, const T & value)
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
        requires ApplicablePredicate< Predicate, T>
    bool Verify( std::string_view  group,
                 std::string_view  id,
                 std::string_view  description,
                 const Predicate & predicate,
                 const T &         value )
    {
        return Verify( Criterion{ group, id, description, predicate }, value);
    }
} // namespace dsl
