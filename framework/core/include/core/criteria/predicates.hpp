#pragma once

#include "core/quantities/bytes.hpp"
#include "core/quantities/quantity.hpp"

#include <array>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <type_traits>

namespace core
{
    //
    // Predicate vocabulary for the framework. A predicate is any callable that
    // takes one or more values and yields bool -- see PredicateFor in
    // criterion.hpp for the concept. These live in core::quantities (reopened
    // from quantity.hpp -- see the comment there) so any rig/device (or a
    // standalone package built on this framework) can reuse them, and so a
    // criteria file can pull in exactly this vocabulary on its own.
    //
    namespace quantities
    {
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

            //
            // "this predicate can be asked about this value" -- the same shape
            // as core::PredicateFor in criterion.hpp, narrowed to one value and
            // restated here rather than included from there.
            //
            // Not an oversight and not duplication for its own sake: the two
            // files are deliberately independent (see the top of this one --
            // a criteria file pulls in this vocabulary on its own, and
            // criterion.hpp includes nothing from here either). Reaching for
            // PredicateFor would make the vocabulary depend on the machinery
            // that holds it, to borrow four lines.
            //
            template<typename Pred, typename T>
            concept CallableWith = requires( const Pred & pred, const T & value)
            {
                { pred( value) } -> std::convertible_to<bool>;
            };
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

            //
            // The one heterogeneous comparison in this file, and constrained so
            // it exists for exactly one expected type.
            //
            // A criteria table has to state an expected payload as a
            // BytePattern, because a CRIT entry is static constexpr and
            // core::Bytes allocates (see BytePattern's own comment). What a
            // Read hands back is a Bytes. Without this overload the two could
            // never meet, and every payload check would have to be written as
            // an ad-hoc Verify in the script -- which is exactly the
            // undeclared, untraceable check the criteria tables exist to
            // replace.
            //
            // This is not the unit-safety rule being relaxed. A Voltage and a
            // Current are different questions, so comparing them is meaningless
            // and rightly refused; a BytePattern and a Bytes are the same
            // octets in two storage shapes, one of which is the only shape a
            // constexpr table can hold. Same argument as MaskPredicate's, and
            // the constraint keeps it to that case: nothing else in this file
            // acquires a cross-type comparison.
            //
            constexpr auto operator()( const Bytes & actual) const -> bool
                requires requires( const T & pattern) { { pattern == actual } -> std::convertible_to<bool>; }
                      && ( !std::same_as<T, Bytes>)
            {
                return expected == actual;
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
        // The types a bit pattern is a meaningful thing to ask about: any
        // integral, plus std::byte -- which is not an integral type, on
        // purpose, and so had to be admitted by name.
        //
        // A float or a Quantity<Unit> stays excluded for the reason MASK was
        // constrained in the first place: masking one is a silent no-op at best
        // and a misleading bit-reinterpretation at worst. What changed is only
        // that a register read a byte at a time out of a serial reply (see
        // core::Bytes::at in core/quantities/bytes.hpp) is now something a criteria table
        // can be written against directly, instead of every call site first
        // converting it to an int and losing the fact that it is one octet.
        //
        template<typename T>
        concept BitPattern = std::integral<T> || std::same_as<T, std::byte>;

        namespace detail
        {
            //
            // Every bit predicate below works in one widest-unsigned type, so a
            // pattern authored as an int and a value that arrives as a
            // std::byte compare as the bit patterns they both are. Widening is
            // lossless in both directions here, and via make_unsigned first so
            // a signed value's pattern is its two's complement rather than an
            // implementation-defined conversion -- the same care
            // core::describeValue takes for the same reason.
            //
            template<BitPattern T>
            constexpr auto toBits( const T value) -> std::uint64_t
            {
                if constexpr( std::same_as<T, std::byte>)
                {
                    return static_cast<std::uint64_t>( std::to_integer<unsigned char>( value));
                }
                else
                {
                    return static_cast<std::uint64_t>( static_cast<std::make_unsigned_t<T>>( value));
                }
            }

            // How many bits a checked value actually has -- what puts BIT_SET<9>()
            // against a std::byte out of range at compile time.
            template<BitPattern T>
            constexpr auto bitWidth() -> unsigned
            {
                return static_cast<unsigned>( sizeof( T) * 8u);
            }
        } // namespace detail

        //
        // MASK (formerly BAND): a bitwise mask/expect check.
        //
        // The pattern's own type and the checked value's type are deliberately
        // independent -- operator() is a template, where every other predicate
        // here takes exactly its T. That is not a relaxation of this file's
        // "an EQ in the wrong unit is a compile error" rule; it is that rule
        // applied to what a bit pattern actually is. A Voltage and a Current
        // are different *quantities*, so comparing them is meaningless and
        // rightly refused. 0x0F is not a quantity at all: it is the same four
        // bits whether the register it is checked against arrives as an
        // unsigned, a std::uint8_t, or one std::byte off a serial reply. Making
        // the criteria author restate the storage type of a value they cannot
        // see from the criteria file would be a spelling requirement, not a
        // safety one -- and MASK( 0x0Fu, 0x05u) in dut/criteria_production.inc
        // means exactly what it always did.
        //
        template<BitPattern T>
        struct MaskPredicate
        {
            T mask;
            T expected;

            template<BitPattern ActualT>
            constexpr auto operator()( const ActualT & value) const -> bool
            {
                return ( detail::toBits( value) & detail::toBits( mask)) == detail::toBits( expected);
            }
        };

        template<BitPattern T>
        constexpr auto MASK( T mask, T expected)
        {
            return MaskPredicate<T>{ mask, expected };
        }

        //
        // BITS_SET / BITS_CLEAR: every bit in the mask is 1, or every bit in
        // the mask is 0. Both are expressible as MASK -- BITS_SET( m) is
        // MASK( m, m) and BITS_CLEAR( m) is MASK( m, 0) -- and exist anyway
        // because a criteria table is read against a specification, and a
        // specification says "bits 2 and 3 must be set", not "the value ANDed
        // with 0x0C equals 0x0C". Stating the mask once instead of twice also
        // removes the way MASK( 0x0Cu, 0x08u) goes wrong: a typo in the second
        // argument is a criterion that still compiles and still looks right.
        //
        enum class BitSense
        {
            Set,
            Clear
        };

        template<BitPattern T, BitSense Sense>
        struct BitsPredicate
        {
            T mask;

            template<BitPattern ActualT>
            constexpr auto operator()( const ActualT & value) const -> bool
            {
                const auto masked = detail::toBits( value) & detail::toBits( mask);

                return ( Sense == BitSense::Set) ? masked == detail::toBits( mask) : masked == 0u;
            }
        };

        template<BitPattern T>
        constexpr auto BITS_SET( T mask)
        {
            return BitsPredicate<T, BitSense::Set>{ mask };
        }

        template<BitPattern T>
        constexpr auto BITS_CLEAR( T mask)
        {
            return BitsPredicate<T, BitSense::Clear>{ mask };
        }

        //
        // BIT_SET<N>() / BIT_CLEAR<N>(): one numbered bit, counting from 0 at
        // the least significant end.
        //
        // The bit number is a template parameter rather than an ordinary
        // argument -- BIT_SET<3>(), not BIT_SET( 3) -- and that is the whole
        // reason these exist as their own predicates instead of being spelled
        // BITS_SET( 1u << 3). A bit number is only meaningful against a value
        // wide enough to have that bit, and as a template parameter it can be
        // checked against one: BIT_SET<9>() applied to a std::byte is a
        // static_assert naming both the bit and the width, where the same
        // mistake written as a runtime argument could only ever have been a
        // criterion that quietly never matches. Same trade, and the same
        // spelling, as hal::DSO8064A's channel<N>() -- see this codebase's rule
        // on what must be a compile error.
        //
        template<unsigned Bit, BitSense Sense>
        struct BitPredicate
        {
            //
            // A requires-clause rather than a static_assert in the body, and
            // the difference is not cosmetic: a static_assert fires when the
            // body is instantiated, which makes "is this bit askable of this
            // value" undetectable to anything that wants to *check* the rule
            // rather than trip over it. As a constraint it is both -- the
            // out-of-range call is still refused at compile time, and
            // framework/core/tests/test_bit_predicates.cpp can demonstrate that it
            // is, which is the only way to show the guarantee is real without
            // breaking the build. Same reasoning as
            // hal::SafeableInstrument in hal/driver/instrument.hpp, which exists to be
            // asserted in both directions for exactly this reason.
            //
            template<BitPattern ActualT>
                requires ( Bit < detail::bitWidth<ActualT>())
            constexpr auto operator()( const ActualT & value) const -> bool
            {
                const auto isSet = ( detail::toBits( value) & ( std::uint64_t{ 1 } << Bit)) != 0u;

                return ( Sense == BitSense::Set) ? isSet : !isSet;
            }
        };

        template<unsigned Bit>
        constexpr auto BIT_SET()
        {
            return BitPredicate<Bit, BitSense::Set>{};
        }

        template<unsigned Bit>
        constexpr auto BIT_CLEAR()
        {
            return BitPredicate<Bit, BitSense::Clear>{};
        }

        //
        // ANY: matches if the actual value equals any one of a fixed list of
        // options. All options must be the same type T -- consistent with
        // EQ/IN/etc. never mixing units -- so `ANY( 3.3_V, 3.6_V, 5.0_V)` is
        // fine but mixing a Voltage in with a Current is a compile error, same
        // as everywhere else in this file.
        //
        // The membership test is a real EqPredicate per option rather than a
        // bare `actual == option`, which is what gives ANY the same
        // `.epsilon(...)` this file's whole family shares -- one tolerance
        // applied to every option, since a list of options is one criterion
        // with one margin, not N criteria each with their own. Delegating is
        // the point rather than an implementation detail: ANY is a disjunction
        // of EQs, so a second hand-written comparison here would be a second
        // place for "equal enough" to (eventually, silently) drift away from
        // what EQ means by it -- the same argument NE's comment makes for
        // being !EQ rather than its own formula.
        //
        // Before this, ANY compared with == unconditionally, which made
        // `ANY( 3.3_V, 5.0_V)` an exact double comparison on a Quantity --
        // precisely the "no tolerance at all isn't the safe default" trap the
        // EQ/NE/... comment above describes, in the one predicate that had no
        // way to opt out of it.
        //
        template<typename T, std::size_t N>
        struct AnyPredicate
        {
            std::array<T, N> options;
            T tolerance = detail::defaultEpsilon<T>();

            constexpr auto epsilon( T eps) const
                requires Toleranced<T>
            {
                auto copy      = *this;
                copy.tolerance = eps;

                return copy;
            }

            constexpr auto operator()( const T & actual) const -> bool
                requires std::equality_comparable<T>
            {
                for( const auto & option : options)
                {
                    if( EqPredicate<T>{ option, tolerance }( actual)) { return true; }
                }

                return false;
            }
        };

        //
        // The one rule ANY and NONE share: every option is the exact same type
        // as the first. Named, and stated once, because NONE is !ANY and
        // "which option lists are legal" has to mean the same thing for both --
        // restating the fold on each factory would be two places to keep in
        // sync for no gain.
        //
        // A concept on the factory rather than a check inside it, for the
        // reason BitPredicate's own comment gives: as a constraint, the
        // illegal case is refused *and* detectable, so
        // framework/core/tests/test_static_constraints.cpp can prove
        // `ANY( 3.3_V, 5.0_A)` is rejected without the proof itself breaking
        // the build. A violation in a function body would be a hard error that
        // no requires-expression can soft-fail.
        //
        template<typename T, typename... Rest>
        concept SameOptions = ( std::same_as<Rest, T> && ...);

        template<typename T, typename... Rest>
            requires SameOptions<T, Rest...>
        constexpr auto ANY( T first, Rest... rest)
        {
            return AnyPredicate<T, 1 + sizeof...( Rest)>{ { first, rest... } };
        }

        //
        // NONE: matches when the value equals none of the options -- exactly
        // !ANY, and built as NotPredicate{ ANY( ...)} for precisely the reason
        // NE is built as NotPredicate{ EQ( ...)}. "None of these" is the
        // complement of "one of these" and nothing more, so a second
        // implementation walking the same list with the comparison inverted
        // would only be a second place for the two to disagree -- and the case
        // where they would disagree first is the interesting one, a
        // borderline value one epsilon away from an option.
        //
        // epsilon() needs no mention here: NotPredicate forwards it to whatever
        // it wraps when that thing has one (see HasEpsilonMethod), so
        // `NONE( 3.3_V, 5.0_V).epsilon( 0.05_V)` works because ANY now has a
        // tolerance, and `NONE( 1, 3).epsilon( 1)` is refused because ANY's is
        // constrained to Toleranced. Neither fact is restated in this file.
        //
        template<typename T, typename... Rest>
            requires SameOptions<T, Rest...>
        constexpr auto NONE( T first, Rest... rest)
        {
            return NotPredicate{ ANY( first, rest...) };
        }

        //
        // ANY_OF: matches when *any one of a list of predicates* matches, where
        // ANY matches a list of values --
        //
        //   ANY_OF( EQ( 5.0_V), IN( 3.0_V, 3.6_V), GT( 12.0_V))
        //
        // This is the disjunction ANY cannot express and never could: ANY's
        // options are values, and a range is not a value. Before it, a
        // criterion like "either the 3V3 band or exactly 5V" had no spelling in
        // a criteria table at all, and had to become an ad-hoc lambda in the
        // script -- the undeclared, untraceable check the tables exist to
        // replace (same argument as EqPredicate's Bytes overload makes).
        //
        // No epsilon() here, deliberately. Each member already carries its own,
        // stated where the value it tolerates is stated:
        //
        //   ANY_OF( EQ( 5.0_V).epsilon( 0.05_V), IN( 3.0_V, 3.6_V).epsilon( 0.1_V))
        //
        // A tolerance on the combinator would have to mean "override every
        // member's", which is both a worse way to write the above and
        // impossible to type -- the members need not share a value type at all
        // (a MASK and an EQ can sit side by side), so there is no single Eps to
        // take.
        //
        // On unit safety, and an honest limit. ANY rejects
        // `ANY( 3.3_V, 5.0_A)` at the factory, via SameOptions. ANY_OF cannot
        // do the same, because a predicate need not have a value type to
        // compare: MASK and BIT_SET<N>() are templates over whatever they are
        // handed, and a rig's own predicate or a lambda has whatever signature
        // it has. So the check lives on operator() instead -- every member must
        // accept the value being checked -- and the mixed-unit mistake is still
        // a compile error, just at the point of use rather than of
        // construction: ANY_OF( EQ( 3.3_V), IN( 1.0_A, 2.0_A)) builds, and is
        // then callable with neither a Voltage nor a Current, so no Verify and
        // no CRIT entry can ever use it. Weaker than ANY's diagnostic, and
        // asserted in both directions in
        // framework/core/tests/test_static_constraints.cpp so the limit is on the
        // record rather than discovered later. The alternative -- a
        // value-type trait specialized per predicate in this file -- would buy
        // the earlier error at the cost of a parallel list that silently stops
        // covering any predicate added without remembering to extend it, which
        // is the trade this file consistently refuses.
        //
        template<typename... Preds>
        struct AnyOfPredicate
        {
            std::tuple<Preds...> predicates;

            template<typename T>
                requires ( detail::CallableWith<Preds, T> && ...)
            constexpr auto operator()( const T & actual) const -> bool
            {
                //
                // A fold over ||, so it short-circuits: the first member that
                // matches is the last one evaluated. That matters beyond speed
                // -- a member may be a rig's own predicate doing something less
                // pure than comparing a number.
                //
                return std::apply(
                    [ &actual]( const Preds &... preds) { return ( preds( actual) || ...); },
                    predicates);
            }
        };

        //
        // At least one member required. An empty ANY_OF() would fold to a
        // constant false -- a criterion that cannot be satisfied by any
        // reading, which is the worst shape of mistake this file can permit:
        // it compiles, it reads like a check, and it fails the run against a
        // perfectly good DUT.
        //
        template<typename... Preds>
            requires ( sizeof...( Preds) > 0)
        constexpr auto ANY_OF( Preds... preds)
        {
            return AnyOfPredicate<Preds...>{ { preds... } };
        }
    } // namespace quantities
} // namespace core
