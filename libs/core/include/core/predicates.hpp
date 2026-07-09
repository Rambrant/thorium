#pragma once

#include "core/quantity.hpp"

#include <array>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <limits>

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
    // are excluded on purpose -- epsilon() below only ever *uses* tolerance
    // for these two cases, so admitting anything else would be a silent no-op.
    //
    template<typename T>
    concept Toleranced = std::floating_point<T> || QuantityType<T>;

    namespace detail
    {
        //
        // The "STL numeric library value" default for a tolerance: machine
        // epsilon for a plain floating_point, or the same thing carried into
        // a Quantity<Unit> (Quantity is backed by double, so we reach for
        // double's epsilon and re-wrap it in-unit). For anything else
        // (int, enum, ...) std::numeric_limits<T>::epsilon() is already 0,
        // which is exactly the "no slack" default an exact type should get.
        //
        template<typename T>
        constexpr auto defaultEpsilon() -> T
        {
            if constexpr( QuantityType<T>)
            {
                return T{ std::numeric_limits<double>::epsilon() };
            }
            else
            {
                return std::numeric_limits<T>::epsilon();
            }
        }

        //
        // Detects whether Pred has its own .epsilon( Eps) -- used by
        // NotPredicate below to forward epsilon() only when the wrapped
        // predicate actually supports it, so e.g. NE( 42).epsilon( 1) fails
        // to compile for the same reason EQ( 42).epsilon( 1) does, with no
        // separate constraint to keep in sync with EqPredicate's own.
        //
        template<typename Pred, typename Eps>
        concept HasEpsilonMethod = requires( Pred pred, Eps eps) { pred.epsilon( eps); };
    } // namespace detail

    //
    // EQ / NE / LT / LE / GT / GE / IN all share one naming convention: the
    // tolerance is a `tolerance` member, set via a fluent `.epsilon(...)`.
    // One name across the whole family, rather than within() on some and
    // epsilon() on others, so it reads the same everywhere:
    //   EQ( 3.3_W).epsilon( 0.05_W)
    //   IN( 3.0_V, 3.6_V).epsilon( 0.05_V)
    //   LT( 5.0_V).epsilon( 0.05_V)
    // All default to machine epsilon (see defaultEpsilon() above) without
    // calling epsilon() at all -- a floating_point/Quantity<Unit> comparison
    // is never really exact, so "no tolerance at all" isn't actually the
    // safe default the way it is for an int. That default is intentionally
    // tiny: it only guards against representation error, not a meaningful
    // physical margin -- call epsilon( 0.05_V) etc. for anything larger.
    //
    template<typename T>
    struct EqPredicate
    {
        T expected;
        T tolerance = detail::defaultEpsilon<T>();

        constexpr auto epsilon( T eps) const
            requires Toleranced<T>
        {
            auto copy      = *this;
            copy.tolerance = eps;

            return copy;
        }

        constexpr auto operator()( const T & actual) const -> bool
        {
            if constexpr( Toleranced<T>)
            {
                using std::abs;

                return abs( actual - expected) <= tolerance;
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

    //
    // Generic logical negation of any predicate. NE is built from this
    // rather than being its own formula (or living in RelationalPredicate
    // next to LT/LE/GT/GE) -- "not equal" is exactly !EQ, nothing more, so
    // writing it as a second, independent implementation would just be a
    // second place for the same idea to (eventually, silently) drift out of
    // sync with EQ. epsilon() is forwarded to the wrapped predicate only
    // when that predicate has one (see HasEpsilonMethod), so NE picks up
    // EQ's Toleranced constraint automatically instead of restating it.
    //
    template<typename Pred>
    struct NotPredicate
    {
        Pred predicate;

        template<typename Eps>
        constexpr auto epsilon( Eps eps) const
            requires detail::HasEpsilonMethod<Pred, Eps>
        {
            auto copy      = *this;
            copy.predicate = predicate.epsilon( eps);

            return copy;
        }

        template<typename T>
        constexpr auto operator()( const T & actual) const -> bool
        {
            return ! predicate( actual);
        }
    };

    template<typename T>
    constexpr auto NE( T value)
    {
        return NotPredicate{ EQ( value) };
    }

    template<typename T>
    struct RangePredicate
    {
        T low;
        T high;
        T tolerance = detail::defaultEpsilon<T>();

        constexpr auto epsilon( T eps) const
            requires Toleranced<T>
        {
            auto copy      = *this;
            copy.tolerance = eps;

            return copy;
        }

        constexpr auto operator()( const T & actual) const -> bool
        {
            if constexpr( Toleranced<T>)
            {
                return actual >= low  - tolerance &&
                       actual <= high + tolerance;
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
    // LT / LE / GT / GE: the STL's own comparison categories
    // (std::partial_ordering et al.) only compare against a *literal* 0 --
    // not a runtime int, not even one forwarded through std::less<> -- so
    // there is no way to parameterize these by a function object the way
    // std::less/std::greater would suggest. Instead, each relation is
    // written out once against a single computed quantity -- either the
    // actual-minus-expected difference (Toleranced branch, below) or a
    // three-way comparison (exact branch) -- and Relation just selects
    // which literal comparison runs.
    //
    // Unlike EQ/NE, there is no way to express "less than" without an
    // ordering, so all four of these are constrained to Orderable
    // (Toleranced or three_way_comparable) -- no weaker option available.
    //
    // This works unmodified for both plain floating_point/integral types and
    // any Quantity<Unit>, since Quantity already defines <=>, -, and
    // unary - -- no separate overload needed per unit.
    //
    enum class Relation { Lt, Le, Gt, Ge };

    template<typename T, Relation R>
    struct RelationalPredicate
    {
        T expected;
        T tolerance = detail::defaultEpsilon<T>();

        constexpr auto epsilon( T eps) const
            requires Toleranced<T>
        {
            auto copy      = *this;
            copy.tolerance = eps;

            return copy;
        }

        constexpr auto operator()( const T & actual) const -> bool
        {
            if constexpr( Toleranced<T>)
            {
                const auto diff = actual - expected;

                if constexpr( R == Relation::Lt) { return diff <  -tolerance; }
                else if constexpr( R == Relation::Le) { return diff <=  tolerance; }
                else if constexpr( R == Relation::Gt) { return diff >   tolerance; }
                else                                   { return diff >= -tolerance; }
            }
            else
            {
                const auto cmp = actual <=> expected;

                if constexpr( R == Relation::Lt) { return cmp <  0; }
                else if constexpr( R == Relation::Le) { return cmp <= 0; }
                else if constexpr( R == Relation::Gt) { return cmp >  0; }
                else                                   { return cmp >= 0; }
            }
        }
    };

    template<typename T>
    concept Orderable = Toleranced<T> || std::three_way_comparable<T>;

    template<Orderable T>
    constexpr auto LT( T value) { return RelationalPredicate<T, Relation::Lt>{ value }; }

    template<Orderable T>
    constexpr auto LE( T value) { return RelationalPredicate<T, Relation::Le>{ value }; }

    template<Orderable T>
    constexpr auto GT( T value) { return RelationalPredicate<T, Relation::Gt>{ value }; }

    template<Orderable T>
    constexpr auto GE( T value) { return RelationalPredicate<T, Relation::Ge>{ value }; }

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