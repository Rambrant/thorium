#include "core/quantities/mask.hpp"
#include "core/quantities/bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <type_traits>

using namespace core;
using namespace core::quantities;

namespace
{
    struct RelayBank {};
    struct FaultWord {};

    using Relays = Mask<RelayBank, std::uint8_t>;
    using Faults = Mask<FaultWord, std::uint16_t>;

    template< typename L, typename R> concept CanOr  = requires( L l, R r) { l | r; };
    template< typename L, typename R> concept CanAnd = requires( L l, R r) { l & r; };
    template< typename L, typename R> concept CanXor = requires( L l, R r) { l ^ r; };
    template< typename L, typename R> concept CanOrAssign = requires( L l, R r) { l |= r; };
}

TEST( CoreMask, BitwiseOperatorsCombineSameTagMasks)
{
    const Relays a{ 0b1100};
    const Relays b{ 0b1010};

    EXPECT_EQ( ( a | b).value(), 0b1110);
    EXPECT_EQ( ( a & b).value(), 0b1000);
    EXPECT_EQ( ( a ^ b).value(), 0b0110);

    Relays c = a;
    c |= b;  EXPECT_EQ( c.value(), 0b1110);
    c &= b;  EXPECT_EQ( c.value(), 0b1010);
    c ^= a;  EXPECT_EQ( c.value(), 0b0110);
}

TEST( CoreMask, ResultStaysInTheMaskTypeAtItsOwnWidth)
{
    static_assert( std::is_same_v<decltype( Relays{ 1} | Relays{ 2}), Relays>);
    static_assert( std::is_same_v<decltype( ~Relays{ 1}),             Relays>);

    // Built-in ~ on a uint8_t promotes to a negative int; here it must not.
    EXPECT_EQ( ( ~Relays{ 0x05}).value(), 0xFA);
    EXPECT_EQ( ( ~Faults{ 0x0001}).value(), 0xFFFE);
}

TEST( CoreMask, DifferentTagsAndBareIntegersAreRefused)
{
    static_assert(   CanOr<Relays, Relays>);
    static_assert( ! CanOr<Relays, Faults>);
    static_assert( ! CanAnd<Relays, Faults>);
    static_assert( ! CanXor<Relays, Faults>);
    static_assert( ! CanOr<Relays, int>);
    static_assert( ! CanOr<int, Relays>);
    static_assert( ! CanOrAssign<Relays, int>);
    static_assert( ! CanOrAssign<Relays, Faults>);

    static_assert( ! std::is_convertible_v<std::uint8_t, Relays>);   // explicit only
    static_assert( ! std::is_convertible_v<Relays, std::uint8_t>);   // and no way back but value()
    static_assert( ! std::is_constructible_v<Relays, Faults>);
}

TEST( CoreMask, ShiftsTakeACountAndNeverOverflowTheWidth)
{
    EXPECT_EQ( ( Relays{ 0b0001} << 3).value(), 0b1000);
    EXPECT_EQ( ( Relays{ 0b1000} >> 3).value(), 0b0001);
    EXPECT_EQ( ( Relays{ 0b1000'0001} << 1).value(), 0b0000'0010);   // top bit shifted out, not kept
    EXPECT_TRUE( ( Relays{ 0xFF} << 8).none());                       // at the width: empty, not UB
    EXPECT_TRUE( ( Relays{ 0xFF} >> 40).none());

    Relays r{ 1};
    r <<= 2;  EXPECT_EQ( r.value(), 4);
    r >>= 1;  EXPECT_EQ( r.value(), 2);
}

TEST( CoreMask, NumberedBitsAndQuestions)
{
    constexpr auto k3 = Relays::bit<3>();
    static_assert( k3.value() == 0b1000);
    static_assert( Faults::bit<15>().value() == 0x8000);

    const Relays closed{ 0b0110};

    EXPECT_TRUE(  closed.contains( Relays::bit<1>() | Relays::bit<2>()));
    EXPECT_FALSE( closed.contains( Relays::bit<1>() | Relays::bit<3>()));
    EXPECT_TRUE(  closed.intersects( Relays::bit<1>() | Relays::bit<3>()));
    EXPECT_FALSE( closed.intersects( Relays::bit<0>()));
    EXPECT_EQ( closed.count(), 2);
    EXPECT_TRUE(  Relays{}.none());
    EXPECT_TRUE(  closed.any());

    EXPECT_EQ( without( closed, Relays::bit<1>()).value(), 0b0100);
    EXPECT_TRUE( Relays{ 3} == Relays{ 3});
    EXPECT_TRUE( Relays{ 3} != Relays{ 4});
}

TEST( CoreMask, AlternativeTokenSpellingsWorkForFree)
{
    const Relays a{ 0b1100};
    const Relays b{ 0b1010};

    EXPECT_EQ( ( a bitor b).value(),  0b1110);
    EXPECT_EQ( ( a bitand b).value(), 0b1000);
    EXPECT_EQ( ( a xor b).value(),    0b0110);
    EXPECT_EQ( ( compl a).value(),    0xF3);
}

TEST( CoreBytesBitwise, CombinesOctetByOctet)
{
    const auto a = Bytes::fromHex( "F0 0F AA");
    const auto b = Bytes::fromHex( "FF 00 55");

    EXPECT_EQ( ( a | b).hex(), "FF 0F FF");
    EXPECT_EQ( ( a & b).hex(), "F0 00 00");
    EXPECT_EQ( ( a ^ b).hex(), "0F 0F FF");
    EXPECT_EQ( ( ~a).hex(),    "0F F0 55");

    Bytes c = a;
    c ^= b;
    EXPECT_EQ( c.hex(), "0F 0F FF");
    EXPECT_EQ( a.hex(), "F0 0F AA");     // the operands are untouched
}

TEST( CoreBytesBitwise, MismatchedLengthsThrowRatherThanPad)
{
    const auto three = Bytes::fromHex( "01 02 03");
    const auto two   = Bytes::fromHex( "01 02");

    EXPECT_THROW( static_cast<void>( three | two), std::invalid_argument);
    EXPECT_THROW( static_cast<void>( three & two), std::invalid_argument);
    EXPECT_THROW( static_cast<void>( three ^ two), std::invalid_argument);
    EXPECT_NO_THROW( static_cast<void>( Bytes{} | Bytes{}));
}

TEST( CoreBytesBitwise, PatternsCombineAtCompileTime)
{
    constexpr auto a = bytes( "AB");
    constexpr auto b = bytes( "\x0F\x0F");

    static_assert( ( a & b).Data[ 0] == std::byte{ 'A' & 0x0F});
    static_assert( ( a | b).Data[ 1] == std::byte{ 'B' | 0x0F});
    static_assert( ( a ^ a).Data[ 0] == std::byte{ 0});
    static_assert( ( ~a).Data[ 0]   == static_cast<std::byte>( ~'A' & 0xFF));

    // A different length is a different type, so it does not compile at all.
    static_assert(   CanOr<BytePattern<2>, BytePattern<2>>);
    static_assert( ! CanOr<BytePattern<2>, BytePattern<3>>);

    EXPECT_TRUE( ( a ^ a) == Bytes::fromHex( "00 00"));
}
