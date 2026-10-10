#pragma once

#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace core
{
    namespace quantities
    {
        //
        // A set of bits that belongs to one particular register or field, as a
        // type of its own -- Quantity<Unit>'s idea, applied to bit patterns.
        //
        // A plain unsigned is how a status register, a relay bank or an
        // interlock word is usually held, and it fails the way a bare double
        // does: the value that means "which relays are closed" and the one
        // that means "which faults are latched" are the same type, so a stray
        // `|` between them compiles and is wrong. The Tag is what makes them
        // different. It is never instantiated; it exists only to be a name:
        //
        //     struct RelayBank {};
        //     struct FaultWord {};
        //     using Relays = Mask<RelayBank, std::uint8_t>;
        //     using Faults = Mask<FaultWord, std::uint16_t>;
        //
        //     Relays closed{ 0x05};
        //     closed |= Relays{ 0x10};            // ok:      same register
        //     closed |= Faults{ 0x10};            // refused: another register's bits
        //     closed |= 0x10;                     // refused: a bare number has no register
        //
        // The same refusal covers a plain integer on either side, because the
        // operators are defined only between two Masks of the same type. That
        // is deliberate and the whole point -- a literal has to be wrapped,
        // once, at the place where it is stated what register it describes.
        //
        // Result type is always the same Mask, at the width of Rep. Built-in
        // bitwise operators promote a narrow unsigned to int, so `~uint8_t{ 5}`
        // is a negative int and `a | b` on two uint16_t is an int; both are
        // converted back here, so a Mask<Tag, uint8_t> never grows or goes
        // negative.
        //
        // Word forms need nothing written: `bitor`, `bitand`, `xor` and `compl`
        // are standard alternative tokens for | & ^ and ~, so
        // `a bitor b` already works on any type that has the symbols.
        //
        // Ordering is deliberately absent -- "is this register's bit pattern
        // less than that one" has no meaning, and a <=> that quietly compared
        // as integers would invite exactly that. Equality is provided, and
        // contains() / intersects() are the questions masks actually answer.
        //
        template< typename Tag, std::unsigned_integral Rep = unsigned int>
        class Mask
        {
            public:
                using tagType = Tag;
                using repType = Rep;

                static constexpr std::size_t Width = std::numeric_limits<Rep>::digits;

                constexpr Mask() = default;

                //
                // Explicit, so a number never becomes a Mask by accident, and
                // taking Rep exactly: a wider or signed argument must be cast
                // by the caller, which is where a truncation or a negative
                // value should be noticed (brace-init narrowing already
                // refuses it for a constant that does not fit).
                //
                constexpr explicit Mask( Rep bits) : mBits( bits) {}

                //
                // One numbered bit, counting from 0 at the least significant
                // end -- the same convention BIT_SET<N>() uses (see
                // core/criteria/predicates.hpp). A template argument for the
                // same reason it is there: a bit beyond the register's width is
                // a compile error, not a shift that quietly does nothing.
                //
                template< std::size_t N> requires ( N < Width)
                [[nodiscard]]
                static constexpr auto bit() -> Mask
                {
                    return Mask{ static_cast<Rep>( Rep{ 1} << N) };
                }

                [[nodiscard]]
                constexpr auto value() const -> Rep
                {
                    return mBits;
                }

                [[nodiscard]] constexpr auto none() const -> bool { return mBits == 0; }
                [[nodiscard]] constexpr auto any()  const -> bool { return mBits != 0; }
                [[nodiscard]] constexpr auto count() const -> int { return std::popcount( mBits); }

                //
                // Every bit of other is set here / at least one is. The two
                // readings of "does this have those bits" that a plain `&` leaves
                // for the reader to spell out, and the usual place to get the
                // difference wrong: (a & m) != 0 is intersects, (a & m) == m
                // is contains.
                //
                [[nodiscard]] constexpr auto contains(   Mask other) const -> bool { return ( mBits & other.mBits) == other.mBits; }
                [[nodiscard]] constexpr auto intersects( Mask other) const -> bool { return ( mBits & other.mBits) != 0; }

                friend constexpr auto operator==( Mask lhs, Mask rhs) -> bool = default;

                friend constexpr auto operator|( Mask lhs, Mask rhs) -> Mask { return Mask{ static_cast<Rep>( lhs.mBits | rhs.mBits) }; }
                friend constexpr auto operator&( Mask lhs, Mask rhs) -> Mask { return Mask{ static_cast<Rep>( lhs.mBits & rhs.mBits) }; }
                friend constexpr auto operator^( Mask lhs, Mask rhs) -> Mask { return Mask{ static_cast<Rep>( lhs.mBits ^ rhs.mBits) }; }
                friend constexpr auto operator~( Mask m)             -> Mask { return Mask{ static_cast<Rep>( ~m.mBits) }; }

                friend constexpr auto operator|=( Mask & lhs, Mask rhs) -> Mask & { return lhs = lhs | rhs; }
                friend constexpr auto operator&=( Mask & lhs, Mask rhs) -> Mask & { return lhs = lhs & rhs; }
                friend constexpr auto operator^=( Mask & lhs, Mask rhs) -> Mask & { return lhs = lhs ^ rhs; }

                //
                // Shifts take a count, not a Mask -- the one place a bare
                // integer belongs on the right of a mask operator. A count at or
                // beyond the register's width yields an empty mask, which is
                // what every bit being shifted out means, rather than the
                // undefined behaviour the built-in shift has there.
                //
                friend constexpr auto operator<<( Mask m, std::size_t count) -> Mask
                {
                    return count >= Width ? Mask{} : Mask{ static_cast<Rep>( m.mBits << count) };
                }

                friend constexpr auto operator>>( Mask m, std::size_t count) -> Mask
                {
                    return count >= Width ? Mask{} : Mask{ static_cast<Rep>( m.mBits >> count) };
                }

                friend constexpr auto operator<<=( Mask & m, std::size_t count) -> Mask & { return m = m << count; }
                friend constexpr auto operator>>=( Mask & m, std::size_t count) -> Mask & { return m = m >> count; }

            private:
                Rep mBits{};
        };

        //
        // "This mask without those bits" -- a & ~b, which is the one
        // combination people write often enough, and get wrong often enough
        // (a & !b, a ^ b), to deserve its name.
        //
        template< typename Tag, std::unsigned_integral Rep>
        [[nodiscard]]
        constexpr auto without( Mask<Tag, Rep> from, Mask<Tag, Rep> bits) -> Mask<Tag, Rep>
        {
            return from & ~bits;
        }

        template<typename T>
        struct is_mask : std::false_type {};

        template< typename Tag, std::unsigned_integral Rep>
        struct is_mask<Mask<Tag, Rep>> : std::true_type {};

        template<typename T>
        concept MaskType = is_mask<T>::value;
    } // namespace quantities
} // namespace core
