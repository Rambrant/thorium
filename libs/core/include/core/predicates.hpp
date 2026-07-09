#pragma once

#include "core/quantity.hpp"

#include <array>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>

namespace core
{
    //
    // Predicate vocabulary for the framework. A predicate is any callable that
    // takes one or more values and yields bool -- see PredicateFor in
    // criterion.hpp for the concept. These live in core so any rig/device (or a
    // standalone package built on this framework) can reuse them.
    //

    //
    // A type an epsilon tolerance is meaningful for: either a plain
    // floating_point, or any Quantity<Unit>. Exact types (ints, enums, ...)
    // are excluded on purpose -- within() below only ever *uses* epsilon for
    // these two cases, so admitting anything else would be a silent no-op.
    //
    template<typename T>
    concept Toleranced = std::floating_point<T> || QuantityType<T>;

    template<typename T>
    struct EqPredicate
    {
        T expected;
        T epsilon{};

        //
        // Constrained to Toleranced: epsilon tolerance is meaningless for
        // exact types (ints, enums, ...), and operator() below only ever
        // *uses* epsilon in the Toleranced branch. Constraining here turns
        // a silent no-op into a compile error instead.
        //
        constexpr auto within( T eps) const
            requires Toleranced<T>
        {
            auto copy    = *this;
            copy.epsilon = eps;

            return copy;
        }

        constexpr auto operator()( const T & actual) const -> bool
        {
            if constexpr( Toleranced<T>)
            {
                using std::abs;

                return abs( actual - expected) <= epsilon;
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
            requires Toleranced<T>
        {
            auto copy    = *this;
            copy.epsilon = eps;

            return copy;
        }

        constexpr auto operator()( const T & actual) const -> bool
        {
            if constexpr( Toleranced<T>)
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

    //
    // LT / LE / GT / GE / NE: the STL's own comparison categories
    // (std::partial_ordering et al.) only compare against a *literal* 0 --
    // not a runtime int, not even one forwarded through std::less<> -- so
    // there is no way to parameterize these by a function object the way
    // std::less/std::greater would suggest. Instead, each relation is
    // written out once, in-unit, against the single `actual <=> expected`
    // computed here; Relation just selects which literal comparison runs.
    // Every relation is expressed in terms of that one <=> -- mirroring how
    // the STL derives a whole ordering from a single three-way comparison --
    // rather than six independent implementations that could individually
    // drift out of sync with each other.
    //
    // This works unmodified for both plain floating_point/integral types and
    // any Quantity<Unit>, since Quantity already defines <=> (and ==, from
    // which != is synthesized) -- no separate overload needed per unit.
    //
    enum class Relation { Lt, Le, Gt, Ge, Ne };

    template<typename T, Relation R>
    struct RelationalPredicate
    {
        T expected;

        constexpr auto operator()( const T & actual) const -> bool
            requires std::three_way_comparable<T>
        {
            const auto cmp = actual <=> expected;

            if constexpr( R == Relation::Lt) { return cmp <  0; }
            else if constexpr( R == Relation::Le) { return cmp <= 0; }
            else if constexpr( R == Relation::Gt) { return cmp >  0; }
            else if constexpr( R == Relation::Ge) { return cmp >= 0; }
            else                                   { return cmp != 0; }
        }
    };

    template<typename T>
    constexpr auto LT( T value) { return RelationalPredicate<T, Relation::Lt>{ value }; }

    template<typename T>
    constexpr auto LE( T value) { return RelationalPredicate<T, Relation::Le>{ value }; }

    template<typename T>
    constexpr auto GT( T value) { return RelationalPredicate<T, Relation::Gt>{ value }; }

    template<typename T>
    constexpr auto GE( T value) { return RelationalPredicate<T, Relation::Ge>{ value }; }

    template<typename T>
    constexpr auto NE( T value) { return RelationalPredicate<T, Relation::Ne>{ value }; }

    //
    // MASK (formerly BAND): a bitwise mask/expect check. Constrained to
    // std::integral -- masking a float or a Quantity<Unit> is not a
    // meaningful operation, so admitting them would be a silent no-op at
    // best and a misleading bit-reinterpretation at worst.
    //
    template<std::integral T>
    struct MaskPredicate
    {
        T mask;
        T expected;

        constexpr auto operator()( const T & value) const -> bool
        {
            return (value & mask) == expected;
        }
    };

    template<std::integral T>
    constexpr auto MASK( T mask, T expected)
    {
        return MaskPredicate<T>{ mask, expected };
    }

    //
    // ANY: matches if the actual value equals any one of a fixed list of
    // options. All options must be the same type T -- consistent with
    // EQ/IN/etc. never mixing units -- so `ANY( 3.3_V, 3.6_V, 5.0_V)` is
    // fine but mixing a Voltage in with a Current is a compile error, same
    // as everywhere else in this file.
    //
    template<typename T, std::size_t N>
    struct AnyPredicate
    {
        std::array<T, N> options;

        constexpr auto operator()( const T & actual) const -> bool
            requires std::equality_comparable<T>
        {
            for( const auto & option : options)
            {
                if( actual == option) { return true; }
            }

            return false;
        }
    };

    template<typename T, typename... Rest>
    constexpr auto ANY( T first, Rest... rest)
        requires ( std::same_as<Rest, T> && ...)
    {
        return AnyPredicate<T, 1 + sizeof...( Rest)>{ { first, rest... } };
    }

} // namespace core