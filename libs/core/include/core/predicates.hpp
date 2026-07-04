#pragma once

#include <cmath>
#include <concepts>

namespace core
{
    //
    // Predicate vocabulary for the framework. A predicate is any callable that
    // takes one or more values and yields bool -- see PredicateFor in
    // criterion.hpp for the concept. These live in core so any rig/device (or a
    // standalone package built on this framework) can reuse them.
    //

    template<typename T>
    struct EqPredicate
    {
        T expected;
        T epsilon{};

        //
        // Constrained to floating_point: epsilon tolerance is meaningless for
        // exact types (ints, enums, ...), and operator() below only ever
        // *uses* epsilon in the floating_point branch. Constraining here turns
        // a silent no-op into a compile error instead.
        //
        constexpr auto within( T eps) const
            requires std::floating_point<T>
        {
            auto copy    = *this;
            copy.epsilon = eps;

            return copy;
        }

        constexpr auto operator()( const T & actual) const -> bool
        {
            if constexpr( std::floating_point<T>)
            {
                return std::abs( actual - expected) <= epsilon;
            }
            else
            {
                return actual == expected;
            }
        }
    };

    template<typename T>
    constexpr auto EQ( T value)
    {
        return EqPredicate<T>{ value };
    }

    template<typename T>
    struct RangePredicate
    {
        T low;
        T high;
        T epsilon{};

        constexpr auto within( T eps) const
            requires std::floating_point<T>
        {
            auto copy    = *this;
            copy.epsilon = eps;

            return copy;
        }

        constexpr auto operator()( const T & actual) const -> bool
        {
            if constexpr( std::floating_point<T>)
            {
                return actual >= low - epsilon &&
                       actual <= high + epsilon;
            }
            else
            {
                return actual >= low &&
                       actual <= high;
            }
        }
    };

    template<typename T>
    constexpr auto IN( T low, T high)
    {
        return RangePredicate<T>{ low, high };
    }

    template<typename T>
    struct BandPredicate
    {
        T mask;
        T expected;

        constexpr auto operator()( const T & value) const -> bool
        {
            return (value & mask) == expected;
        }
    };

    template<typename T>
    constexpr auto BAND( T mask, T expected)
    {
        return BandPredicate<T>{ mask, expected };
    }
} // namespace core
