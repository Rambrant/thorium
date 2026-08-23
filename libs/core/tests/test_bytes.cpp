#include "core/bytes.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

//
// core::Bytes is the payload every byte-oriented verb carries -- what Write
// sends and what Read hands back (see core/transfer.hpp). These tests are about
// the two claims the type makes that a std::string of the same octets would
// not: that a byte sequence is compared and indexed as octets rather than as
// text, and that a payload which cannot be read as text says so when it is
// written into a log rather than rendering as line noise.
//

using core::Bytes;

TEST( CoreBytes, ATextLiteralBecomesItsOctets)
{
    //
    // The implicit conversion exists so a protocol document's own spelling
    // reaches a Write() call site unchanged -- see the class's own comment.
    //
    const Bytes payload = "RD 30\r";

    ASSERT_EQ( payload.size(), 6u);
    EXPECT_EQ( payload.at( 0), std::byte{ 'R' });
    EXPECT_EQ( payload.at( 5), std::byte{ 0x0D });
    EXPECT_EQ( payload.text(), "RD 30\r");
}

//
// The case a std::string would get wrong. strlen stops at the first NUL, so a
// binary frame carrying one would silently lose everything after it; a size
// taken from the sequence itself does not.
//
TEST( CoreBytes, AnEmbeddedNulIsAnOrdinaryByte)
{
    const auto frame = Bytes::fromHex( "02 00 41 00 03");

    ASSERT_EQ( frame.size(), 5u);
    EXPECT_EQ( frame.at( 1), std::byte{ 0x00 });
    EXPECT_EQ( frame.at( 4), std::byte{ 0x03 });
}

TEST( CoreBytes, HexParsesWithOrWithoutSpacing)
{
    EXPECT_EQ( Bytes::fromHex( "1B 5B 41"), Bytes::fromHex( "1b5b41"));
    EXPECT_EQ( Bytes::fromHex( "1B5B41").hex(), "1B 5B 41");
    EXPECT_TRUE( Bytes::fromHex( "").empty());
}

//
// Half a byte is a mistake in the protocol document or in the test, and either
// way the wrong thing to do is to accept it and drop the nibble.
//
TEST( CoreBytes, MalformedHexThrowsRatherThanTruncating)
{
    EXPECT_THROW( (void) Bytes::fromHex( "1B5"),  std::invalid_argument);
    EXPECT_THROW( (void) Bytes::fromHex( "1BZZ"), std::invalid_argument);
}

//
// The failure this type exists around: a reply that came back shorter than the
// protocol says. Reading past its end is a std::out_of_range the script author
// sees, never whatever happened to be in the allocation.
//
TEST( CoreBytes, ReadingPastTheEndThrows)
{
    const Bytes reply = "OK";

    EXPECT_EQ( reply.at( 1), std::byte{ 'K' });
    EXPECT_THROW( (void) reply.at( 2), std::out_of_range);
    EXPECT_THROW( (void) reply.slice( 1, 5), std::out_of_range);
}

//
// count is checked against the remaining length rather than offset + count
// being checked against the size, so a caller passing a huge count cannot wrap
// the addition round and appear to be in range.
//
TEST( CoreBytes, ASliceCannotWrapItsWayIntoRange)
{
    const Bytes payload = "ABCD";

    EXPECT_EQ( payload.slice( 1, 2), Bytes( "BC"));
    EXPECT_THROW( (void) payload.slice( 1, static_cast<std::size_t>( -1)), std::out_of_range);
}

TEST( CoreBytes, BeforeStripsAtTheFirstOccurrenceOnly)
{
    const Bytes reply = "0xF5\rOK\r";

    EXPECT_EQ( reply.before( "\r"), Bytes( "0xF5"));
    EXPECT_EQ( reply.before( "\n"), reply);          // absent -- the whole thing
}

//
// An unconfigured terminator arrives here as an empty delimiter, and returning
// an empty payload for it would silently discard the whole reply.
//
TEST( CoreBytes, AnEmptyDelimiterNeverOccurs)
{
    const Bytes reply = "OK\r";

    EXPECT_EQ( reply.before( Bytes{}), reply);
}

TEST( CoreBytes, PrefixAndSuffixAreLengthChecked)
{
    const Bytes reply = "ACK\r";

    EXPECT_TRUE(  reply.startsWith( "ACK"));
    EXPECT_TRUE(  reply.endsWith( "\r"));
    EXPECT_FALSE( reply.startsWith( "ACK\r\n"));     // longer than the payload
    EXPECT_FALSE( reply.endsWith( "ACK\r\n"));
}

//
// Equality is over the octets, so two payloads that differ only in a trailing
// terminator are two different replies -- the distinction a criterion checking
// an acknowledgement depends on.
//
TEST( CoreBytes, EqualityIsOverTheOctets)
{
    EXPECT_EQ( Bytes( "OK"), Bytes::fromHex( "4F4B"));
    EXPECT_NE( Bytes( "OK"), Bytes( "OK\r"));
    EXPECT_NE( Bytes( "OK"), Bytes( "ok"));
}

//
// The log rendering rule, and the reason it is chosen per payload rather than
// per byte: a text console has to read as text, and a binary frame has to read
// as a frame with a countable number of bytes in it.
//
TEST( CoreBytes, ATextPayloadIsLoggedAsEscapedText)
{
    EXPECT_EQ( core::describeValue( Bytes( "RD 30\r")), "\"RD 30\\r\"");
    EXPECT_EQ( core::describeValue( Bytes( "a\tb\n")),  "\"a\\tb\\n\"");
    EXPECT_EQ( core::describeValue( Bytes( "")),        "\"\"");
}

TEST( CoreBytes, ABackslashOrQuoteInThePayloadIsEscaped)
{
    //
    // So a reader can never mistake a protocol's own backslash for the start
    // of one of the escapes above.
    //
    EXPECT_EQ( core::describeValue( Bytes( "a\\b")), "\"a\\\\b\"");
    EXPECT_EQ( core::describeValue( Bytes( "say \"hi\"")), "\"say \\\"hi\\\"\"");
}

TEST( CoreBytes, ABinaryPayloadIsLoggedAsHex)
{
    //
    // One non-renderable byte is enough to switch the whole payload over --
    // the rule is per payload, so a reader never has to work out which half of
    // a line is text and which is an escape.
    //
    EXPECT_EQ( core::describeValue( Bytes::fromHex( "1B 5B 41")), "<1B 5B 41>");
    EXPECT_EQ( core::describeValue( Bytes( std::vector<std::byte>{ std::byte{ 'O' }, std::byte{ 0x00 } })), "<4F 00>");
}

//
// The bound on the rendering -- see kMaxDescribedBody in core/bytes.hpp. A
// description is what a reader reads; the payload itself survives in full in
// the recording, which is what a replay feeds from.
//
TEST( CoreBytes, AShortPayloadIsRenderedInFull)
{
    //
    // The bound has to be invisible for everything a console protocol actually
    // exchanges, or every existing log line changes shape for nothing.
    //
    const auto reply = Bytes( "OK RD 30 = 0xF5\r\n");

    EXPECT_EQ( core::describeValue( reply), "\"OK RD 30 = 0xF5\\r\\n\"");
    EXPECT_LT( core::describeValue( reply).size(), core::kMaxDescribedBody);
}

TEST( CoreBytes, ALongTextPayloadIsAbridgedAndSaysHowLongItWas)
{
    const auto banner = Bytes( std::string( 413, 'a'));

    const auto described = core::describeValue( banner);

    EXPECT_TRUE( described.starts_with( "\"aaaa"));
    EXPECT_TRUE( described.ends_with( "...\" (413 bytes)")) << described;

    //
    // The count is what makes the abridging non-destructive for a reader: the
    // one fact that cannot be recovered by looking at the head is how much of
    // the payload the head is.
    //
    EXPECT_NE( described.find( "413"), std::string::npos);
}

TEST( CoreBytes, ALongBinaryPayloadIsAbridgedAsHex)
{
    auto frame = std::vector<std::byte>( 4096, std::byte{ 0xAB });

    frame.front() = std::byte{ 0x1B };

    const auto described = core::describeValue( Bytes( frame));

    EXPECT_TRUE( described.starts_with( "<1B AB AB")) << described;
    EXPECT_TRUE( described.ends_with( " ...> (4096 bytes)")) << described;
}

TEST( CoreBytes, AnAbridgedRenderingStaysWithinTheBoundPlusItsCount)
{
    //
    // The head is bounded and the byte count is not, so what is promised is
    // the head plus the delimiters and the number -- comfortably inside
    // core::kMaxJournalValueLength, which is the backstop underneath this one.
    //
    const auto described = core::describeValue( Bytes( std::string( 100000, 'x')));

    EXPECT_LT( described.size(), core::kMaxDescribedBody + 32);
}

TEST( CoreBytes, ATextHeadOnABinaryPayloadIsStillRenderedAsBinary)
{
    //
    // The encoding is decided from the whole payload, not from the head that
    // gets rendered. A firmware image beginning with an ASCII banner must not
    // be announced in quotes -- a reader would take the abridged head for the
    // start of a string and the rest for more of the same.
    //
    auto image = std::vector<std::byte>{};

    for( const auto c : std::string_view( "BOOTLOADER v2.1 READY"))
    {
        image.push_back( static_cast<std::byte>( c));
    }

    image.resize( 512, std::byte{ 0xFF });

    EXPECT_TRUE( core::describeValue( Bytes( image)).starts_with( "<42 4F 4F 54"));
}
