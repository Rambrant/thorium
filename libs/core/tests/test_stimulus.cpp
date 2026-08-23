#include "core/stimulus.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/bytes.hpp"
#include "core/session.hpp"
#include "core/waveform.hpp"

//
// The authored half of the injection story -- core/stimulus.hpp. A recording is
// captured from a run and consumed once per row; this is written by hand, and
// its whole reason for existing is the two things a recording cannot say: "this
// point always reads X", and "this trace is these samples" without spelling
// them in hex.
//

using core::Bytes;
using core::quantities::Current;
using core::quantities::Time;
using core::quantities::Voltage;

namespace
{
    //
    // Arms a bank from stimulus text and hands it back, so each test below
    // reads as the thing it is asserting rather than as four lines of setup.
    //
    auto armed( const std::string & text, const std::filesystem::path & base = {}) -> core::SessionBank
    {
        core::SessionBank  bank;
        std::istringstream in( text);

        core::injectStimulus( bank, in, base);

        return bank;
    }

    auto voltageAt( core::SessionBank & bank, const std::string & key) -> double
    {
        return core::asQuantity<Voltage>(
            bank.active().fetch( key, "Dmm1", core::quantityKindOf<Voltage>(),
                                 []{ return core::QuantityVariant{ Voltage{ 0.0 } }; })).value();
    }
} // namespace

// ---------------------------------------------------------------------------
// The unit vocabulary
// ---------------------------------------------------------------------------

//
// The symbols a stimulus file accepts are core::quantities' own, derived from
// the unit tags rather than restated -- so this cannot drift from what a log
// line prints, which is the property that lets a value be pasted from one into
// the other.
//
TEST( CoreStimulus, TheSymbolTableIsTheUnitTagsOwn)
{
    EXPECT_EQ( core::unitSymbol( core::quantityKindOf<Voltage>()), "V");
    EXPECT_EQ( core::unitSymbol( core::quantityKindOf<Time>()),    "s");

    EXPECT_EQ( core::quantityKindFromSymbol( "V"),   core::quantityKindOf<Voltage>());
    EXPECT_EQ( core::quantityKindFromSymbol( "A"),   core::quantityKindOf<Current>());
    EXPECT_EQ( core::quantityKindFromSymbol( "Ohm"), core::quantityKindOf<core::quantities::Resistance>());

    EXPECT_FALSE( core::quantityKindFromSymbol( "Volts").has_value());
    EXPECT_FALSE( core::quantityKindFromSymbol( "mV").has_value());
}

TEST( CoreStimulus, TheEmptySymbolIsTheDimensionlessKind)
{
    //
    // Which is what makes a bare number in a stimulus file mean something. Its
    // symbol is genuinely "" (see core/quantity.hpp), not missing.
    //
    EXPECT_EQ( core::quantityKindFromSymbol( ""), core::quantityKindOf<core::quantities::PowerFactor>());
}

// ---------------------------------------------------------------------------
// Sticky versus sequence -- the rule the whole format exists for
// ---------------------------------------------------------------------------

TEST( CoreStimulus, OneValueIsStickyAndNeverRunsOut)
{
    //
    // The thing a recording cannot express. A --repeat=50 run against a
    // recording needs fifty copies of every row; against this it needs one
    // line.
    //
    auto bank = armed( "Output5V = 5.01 V\n");

    for( auto pass = 0; pass < 50; ++pass)
    {
        EXPECT_DOUBLE_EQ( voltageAt( bank, "Output5V"), 5.01);
    }
}

TEST( CoreStimulus, AListIsConsumedInOrderAndThenRunsOut)
{
    auto bank = armed( "Output5V = 5.01 V, 4.90 V\n");

    EXPECT_DOUBLE_EQ( voltageAt( bank, "Output5V"), 5.01);
    EXPECT_DOUBLE_EQ( voltageAt( bank, "Output5V"), 4.90);

    // A script reading more times than the author answered for has diverged
    // from what was expected -- the same hard error every other seam gives.
    EXPECT_THROW( (void) voltageAt( bank, "Output5V"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// The four shapes, and that the shape picks the seam
// ---------------------------------------------------------------------------

TEST( CoreStimulus, AFlagArmsTheAwaitSeam)
{
    auto bank = armed( "Osc1.Acquisition = true\n");

    EXPECT_TRUE( bank.active().fetchFlag( "Osc1.Acquisition", "Osc1", []{ return false; }));
}

TEST( CoreStimulus, APayloadIsSpelledTheWayALogSpellsIt)
{
    //
    // Both forms are core::describeValue's own output, character for character,
    // so a reply can be read off a log line and pasted straight in.
    //
    auto text = armed( "Ser1.Data = \"ACK\\r\"\n");
    auto hex  = armed( "Ser1.Data = <41 43 4B 0D 08>\n");

    EXPECT_EQ( text.active().fetchData( "Ser1.Data", "Ser1", []{ return Bytes{}; }), Bytes( "ACK\r"));
    EXPECT_EQ( hex.active().fetchData(  "Ser1.Data", "Ser1", []{ return Bytes{}; }), Bytes::fromHex( "41434B0D08"));
}

TEST( CoreStimulus, AnEmptyPayloadIsWritable)
{
    //
    // A port that answered nothing before its timeout is a real observation,
    // and has to be sayable.
    //
    auto bank = armed( "Ser1.Data = <>\n");

    EXPECT_TRUE( bank.active().fetchData( "Ser1.Data", "Ser1", []{ return Bytes( "live"); }).empty());
}

TEST( CoreStimulus, ATraceIsDescribedRatherThanEncoded)
{
    auto bank = armed( "Osc1.Channel3 = trace( V, -0.001 s, 1e-06 s, [ 5.0 4.6 4.8 ])\n");

    const auto trace = bank.active().fetchTrace( "Osc1.Channel3", "Osc1", []{ return core::Waveform{}; });

    ASSERT_EQ( trace.size(), 3u);
    EXPECT_EQ( trace.kind(), core::quantityKindOf<Voltage>());
    EXPECT_DOUBLE_EQ( trace.timing().Origin.value(),    -0.001);
    EXPECT_DOUBLE_EQ( trace.timing().Increment.value(), 1e-06);
    EXPECT_DOUBLE_EQ( trace.minimum<Voltage>().value(), 4.6);
}

TEST( CoreStimulus, ATracesSamplesCanComeFromAFileBesideIt)
{
    //
    // The path that matters for a real trace: four thousand numbers are not
    // going in a stimulus line, and whatever produced them -- a scope export, a
    // spreadsheet column, a model, a generator -- already writes numbers.
    //
    const auto dir = std::filesystem::temp_directory_path() / "thorium-stimulus-samples";

    std::filesystem::remove_all( dir);
    std::filesystem::create_directories( dir);

    {
        std::ofstream samples( dir / "dip.samples");

        samples << "5.0 5.0\n4.6\n  4.8  \n5.0\n";
    }

    auto bank = armed( "Osc1.Channel3 = trace( V, 0 s, 1e-06 s, \"dip.samples\")\n", dir);

    const auto trace = bank.active().fetchTrace( "Osc1.Channel3", "Osc1", []{ return core::Waveform{}; });

    EXPECT_EQ( trace.size(), 5u);
    EXPECT_DOUBLE_EQ( trace.minimum<Voltage>().value(), 4.6);

    std::filesystem::remove_all( dir);
}

// ---------------------------------------------------------------------------
// Layering, comments, and what gets rejected
// ---------------------------------------------------------------------------

TEST( CoreStimulus, ItIsAdditiveSoItCanBeLayeredOverALoadedRecording)
{
    //
    // The reason --inject and --replay combine: re-running a captured failure
    // with one reading changed is how you find out whether that reading was the
    // cause.
    //
    core::SessionBank bank;

    bank.inject( "Output5V",  core::QuantityVariant{ Voltage{ 5.02 } });
    bank.inject( "Output3V3", core::QuantityVariant{ Voltage{ 3.30 } });

    std::istringstream in( "Output5V = 4.90 V\n");

    core::injectStimulus( bank, in);

    EXPECT_DOUBLE_EQ( voltageAt( bank, "Output5V"),  4.90);
    EXPECT_DOUBLE_EQ( voltageAt( bank, "Output3V3"), 3.30);
}

TEST( CoreStimulus, CommentsAndBlankLinesAreSkippedButNotInsideAPayload)
{
    auto bank = armed(
        "# what the DUT should say\n"
        "\n"
        "Output5V  = 5.01 V   # the nominal\n"
        "Ser1.Data = \"#3 OK\"\n");

    EXPECT_DOUBLE_EQ( voltageAt( bank, "Output5V"), 5.01);
    EXPECT_EQ( bank.active().fetchData( "Ser1.Data", "Ser1", []{ return Bytes{}; }), Bytes( "#3 OK"));
}

TEST( CoreStimulus, NothingIsProgrammedWhenALaterLineIsBad)
{
    //
    // A bank armed from half a file fails somewhere unrelated to the typo --
    // "nothing programmed for X", three scripts later -- which is the diagnosis
    // the parse-everything-first ordering exists to prevent.
    //
    core::SessionBank  bank;
    std::istringstream in( "Output5V = 5.01 V\nOutput3V3 = nonsense\n");

    EXPECT_THROW( core::injectStimulus( bank, in), std::runtime_error);

    // Still live: nothing was armed, so this reaches the callback.
    EXPECT_DOUBLE_EQ( voltageAt( bank, "Output5V"), 0.0);
}

TEST( CoreStimulus, AFailureNamesTheLineItsNumberAndWhatIsWrong)
{
    core::SessionBank  bank;
    std::istringstream in( "Output5V = 5.01 V\nOutput3V3 = 3.3 Volts\n");

    try
    {
        core::injectStimulus( bank, in);

        ADD_FAILURE() << "'Volts' is not a unit symbol and should not be accepted";
    }
    catch( const std::runtime_error & error)
    {
        const std::string message = error.what();

        EXPECT_NE( message.find( "line 2"),   std::string::npos) << message;
        EXPECT_NE( message.find( "Volts"),    std::string::npos) << message;
        EXPECT_NE( message.find( "Output3V3"), std::string::npos) << message;
    }
}

TEST( CoreStimulus, AKeyWithASpaceInItIsRejected)
{
    //
    // No session key contains whitespace, so this is a typo -- and accepting it
    // would arm a slot nothing ever asks for, surfacing later as "nothing
    // programmed for 'Output5V'" pointing away from the line at fault.
    //
    core::SessionBank  bank;
    std::istringstream in( "Output 5V = 5.01 V\n");

    EXPECT_THROW( core::injectStimulus( bank, in), std::runtime_error);
}

TEST( CoreStimulus, MixingShapesInOneListIsRejected)
{
    //
    // A key answers one kind of question. Programming both seams would leave
    // the second silently unreachable.
    //
    core::SessionBank  bank;
    std::istringstream in( "Output5V = 5.01 V, true\n");

    EXPECT_THROW( core::injectStimulus( bank, in), std::runtime_error);
}

TEST( CoreStimulus, MalformedValuesAreRejectedRatherThanPartlyRead)
{
    for( const auto * line : { "K = 5.01 VV\n",
                               "K = \"unterminated\n",
                               "K = <41 4>\n",
                               "K = <ZZ>\n",
                               "K = trace( V, 0 s, 1e-06 s)\n",
                               "K = trace( V, 0 V, 1e-06 s, [ 1 ])\n",
                               "K = trace( V, 0 s, 1e-06 s, 5.0)\n",
                               "K = \"bad \\q escape\"\n",
                               "K\n",
                               "= 5 V\n" })
    {
        core::SessionBank  bank;
        std::istringstream in( line);

        EXPECT_THROW( core::injectStimulus( bank, in), std::runtime_error) << "line: " << line;
    }
}
