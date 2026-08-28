#include "core/criteria/predicates.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "core/quantities/bytes.hpp"
#include "core/quantities/format.hpp"
#include "core/criteria/predicate_text.hpp"

//
// The bit-pattern half of the predicate vocabulary: MASK widened to accept a
// std::byte, and the BITS_SET/BITS_CLEAR/BIT_SET/BIT_CLEAR spellings a
// specification that numbers its bits is actually written in.
//
// What these are here to pin down is the one deliberate asymmetry with the rest
// of core/criteria/predicates.hpp: every other predicate insists the checked value have
// exactly the authored type, and these do not. See MaskPredicate's own comment
// for why -- 0x0F is the same four bits whichever width the register arrives
// in, where a Voltage and a Current are genuinely different questions.
//

using namespace core::quantities;

TEST( CoreBitPredicates, MaskStillMeansWhatItDidForIntegers)
{
    //
    // dut/criteria_production.inc's own entries, unchanged -- widening MASK
    // must not have moved what an existing criteria table asserts.
    //
    constexpr auto lowNibbleIsFive = MASK( 0x0Fu, 0x05u);

    EXPECT_TRUE(  lowNibbleIsFive( 0xF5u));
    EXPECT_TRUE(  lowNibbleIsFive( 0x05u));
    EXPECT_FALSE( lowNibbleIsFive( 0xF6u));
}

//
// The point of the widening: a criteria file is authored in hex against a
// specification, and the value reaches it as whatever the instrument handed
// back -- one octet of a serial reply, here.
//
TEST( CoreBitPredicates, AnIntAuthoredMaskChecksAByte)
{
    constexpr auto lowNibbleIsFive = MASK( 0x0Fu, 0x05u);

    EXPECT_TRUE(  lowNibbleIsFive( std::byte{ 0xF5 }));
    EXPECT_FALSE( lowNibbleIsFive( std::byte{ 0xF6 }));
}

TEST( CoreBitPredicates, AByteAuthoredMaskChecksAnInt)
{
    constexpr auto lowNibbleIsFive = MASK( std::byte{ 0x0F }, std::byte{ 0x05 });

    EXPECT_TRUE(  lowNibbleIsFive( 0xF5u));
    EXPECT_TRUE(  lowNibbleIsFive( std::byte{ 0x05 }));
    EXPECT_FALSE( lowNibbleIsFive( 0xF6u));
}

//
// Through make_unsigned first, so a signed value's pattern is its two's
// complement rather than an implementation-defined conversion -- the same care
// core::describeValue takes.
//
TEST( CoreBitPredicates, ASignedValuesPatternIsItsTwosComplement)
{
    constexpr auto topByteIsFF = MASK( 0xFFu, 0xFFu);

    EXPECT_TRUE( topByteIsFF( static_cast<std::int8_t>( -1)));
}

TEST( CoreBitPredicates, BitsSetAndBitsClearAskAboutEveryBitInTheMask)
{
    constexpr auto bothSet   = BITS_SET(   0x0Cu);
    constexpr auto bothClear = BITS_CLEAR( 0x0Cu);

    EXPECT_TRUE(  bothSet( 0x0Cu));
    EXPECT_TRUE(  bothSet( 0xFFu));
    EXPECT_FALSE( bothSet( 0x08u));      // only one of the two
    EXPECT_FALSE( bothSet( 0x00u));

    EXPECT_TRUE(  bothClear( 0x00u));
    EXPECT_TRUE(  bothClear( 0xF3u));
    EXPECT_FALSE( bothClear( 0x08u));    // one of the two is set
}

TEST( CoreBitPredicates, BitSetAndBitClearCountFromZeroAtTheLeastSignificantEnd)
{
    EXPECT_TRUE(  BIT_SET<0>()( std::byte{ 0x01 }));
    EXPECT_TRUE(  BIT_SET<7>()( std::byte{ 0x80 }));
    EXPECT_FALSE( BIT_SET<7>()( std::byte{ 0x7F }));

    EXPECT_TRUE(  BIT_CLEAR<7>()( std::byte{ 0x7F }));
    EXPECT_FALSE( BIT_CLEAR<0>()( std::byte{ 0x01 }));
}

//
// A bit predicate is width-agnostic in the same way MASK is -- bit 3 is bit 3
// whether the status register arrives as a byte or as an int.
//
TEST( CoreBitPredicates, ABitPredicateChecksEitherWidth)
{
    constexpr auto ready = BIT_SET<3>();

    EXPECT_TRUE( ready( std::byte{ 0x08 }));
    EXPECT_TRUE( ready( 0x08u));
    EXPECT_TRUE( ready( std::uint16_t{ 0x0108 }));
}

//
// The reason the bit number is a template parameter rather than an argument:
// asking for bit 9 of a std::byte is a mistake the compiler can see, and the
// same mistake written as BIT_SET( 9) could only ever have been a criterion
// that quietly never matched.
//
TEST( CoreBitPredicates, AnOutOfRangeBitIsRejectedAtCompileTime)
{
    constexpr auto checkable = []< typename T>( T value)
    {
        return requires { BIT_SET<9>()( value); };
    };

    EXPECT_FALSE( checkable( std::byte{ 0x00 }));       // 8 bits -- no bit 9
    EXPECT_TRUE(  checkable( std::uint16_t{ 0 }));      // 16 bits -- fine
}

//
// A float or a Quantity<Unit> stays out, which is why MASK was constrained in
// the first place: masking one is a silent no-op at best.
//
TEST( CoreBitPredicates, NonPatternTypesAreStillRefused)
{
    //
    // Asked through a generic lambda, not a bare requires-expression: an
    // invalid expression that depends on no template parameter is a hard error
    // rather than an unsatisfied requirement, so the question has to be posed
    // somewhere T is still open.
    //
    constexpr auto maskable   = []< typename T>( T v) { return requires { MASK( v, v); }; };
    constexpr auto bitsable   = []< typename T>( T v) { return requires { BITS_SET( v); }; };
    constexpr auto checkable  = []< typename T>( T v) { return requires { BIT_SET<0>()( v); }; };

    EXPECT_FALSE( maskable(  1.5));
    EXPECT_FALSE( bitsable(  1.5));
    EXPECT_FALSE( checkable( 1.5));

    EXPECT_TRUE(  maskable(  0x0Fu));
    EXPECT_TRUE(  checkable( std::byte{ 0x01 }));
}

//
// A criterion has to be able to state its own limit in the report -- otherwise
// the log carries only the prose a CRIT author wrote, which nothing checks
// against the predicate beside it.
//
TEST( CoreBitPredicates, EachBitPredicateSpellsItselfOutForTheLog)
{
    EXPECT_EQ( core::describeCriterion( MASK( 0x0Fu, 0x05u)),   "(value & 0xF) == 0x5");
    EXPECT_EQ( core::describeCriterion( BITS_SET(   0x0Cu)),    "bits 0xC set");
    EXPECT_EQ( core::describeCriterion( BITS_CLEAR( 0x0Cu)),    "bits 0xC clear");
    EXPECT_EQ( core::describeCriterion( BIT_SET<3>()),          "bit 3 set");
    EXPECT_EQ( core::describeCriterion( BIT_CLEAR<7>()),        "bit 7 clear");
}

//
// A byte-authored mask describes itself as a pattern too, not as the decimal
// the widening runs through.
//
TEST( CoreBitPredicates, AByteAuthoredMaskDescribesItselfAsAPattern)
{
    EXPECT_EQ( core::describeCriterion( MASK( std::byte{ 0x0F }, std::byte{ 0x05 })), "(value & 0xF) == 0x5");
}

//
// A byte is a fixed-width field, so it is written down as one -- a report is
// read by scanning a column of them.
//
TEST( CoreBitPredicates, AByteIsLoggedAsExactlyTwoHexDigits)
{
    EXPECT_EQ( core::describeValue( std::byte{ 0x05 }), "0x05");
    EXPECT_EQ( core::describeValue( std::byte{ 0xF5 }), "0xF5");
    EXPECT_EQ( core::numericOf( std::byte{ 0xF5 }),     245.0);
}
