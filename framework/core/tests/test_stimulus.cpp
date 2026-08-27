#include "core/stimulus.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

    //
    // A directory that takes itself away again, so the sample-file tests below
    // read as what they are asserting rather than as four lines of filesystem
    // bookkeeping -- and so a test that fails part way through still leaves the
    // temporary directory clean.
    //
    struct TemporaryDirectory
    {
        explicit TemporaryDirectory( const std::string & name)
            : Path( std::filesystem::temp_directory_path() / name)
        {
            std::filesystem::remove_all( Path);
            std::filesystem::create_directories( Path);
        }

        ~TemporaryDirectory() { std::filesystem::remove_all( Path); }

        TemporaryDirectory( const TemporaryDirectory &)                     = delete;
        auto operator=( const TemporaryDirectory &) -> TemporaryDirectory & = delete;

        auto write( const std::string & name, const std::string & content) const -> std::filesystem::path
        {
            std::ofstream out( Path / name);

            out << content;

            return Path / name;
        }

        std::filesystem::path  Path;
    };

    //
    // The message rather than only the type. A sample file's diagnostics are
    // most of why core::readSamples is public instead of a two-line ifstream in
    // every test that needs one, so what they actually say is worth asserting.
    //
    template<typename CallableT>
    auto messageFrom( CallableT && callable) -> std::string
    {
        try
        {
            std::forward<CallableT>( callable)();
        }
        catch( const std::exception & error)
        {
            return error.what();
        }

        return "<nothing was thrown>";
    }

    auto mentions( const std::string & message, const std::string_view fragment) -> bool
    {
        return message.find( fragment) != std::string::npos;
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
    const TemporaryDirectory  dir{ "thorium-stimulus-samples" };

    dir.write( "dip.samples", "5.0 5.0\n4.6\n  4.8  \n5.0\n");

    auto bank = armed( "Osc1.Channel3 = trace( V, 0 s, 1e-06 s, \"dip.samples\")\n", dir.Path);

    const auto trace = bank.active().fetchTrace( "Osc1.Channel3", "Osc1", []{ return core::Waveform{}; });

    EXPECT_EQ( trace.size(), 5u);
    EXPECT_DOUBLE_EQ( trace.minimum<Voltage>().value(), 4.6);
}

// ---------------------------------------------------------------------------
// A sample file without a stimulus file around it
// ---------------------------------------------------------------------------
//
// core::readSamples and core::traceFromFile: the file half of a trace, reached
// directly. What a script unit test uses when the capture it is checking
// against is a real one -- Fetch.inject takes a Waveform, and four thousand
// samples do not go in a source file.
//

TEST( CoreStimulus, SamplesComeOffDiskInOrderHoweverTheyAreLaidOut)
{
    const TemporaryDirectory  dir{ "thorium-read-samples" };

    //
    // Several to a line, one to a line, and padded -- a scope export, a
    // spreadsheet column and a hand-written file in one, none of which should
    // need reformatting to be read.
    //
    const auto path = dir.write( "dip.samples", "5.0 4.9\n4.6\n  4.8  \n-1e-03\n");

    EXPECT_EQ( core::readSamples( path), (std::vector<double>{ 5.0, 4.9, 4.6, 4.8, -1e-03 }));
}

TEST( CoreStimulus, ATraceIsBuiltFromASampleFileWithoutAuthoringAStimulusFile)
{
    const TemporaryDirectory  dir{ "thorium-trace-from-file" };

    const auto path = dir.write( "dip.samples", "5.0 4.6 4.8\n");

    const auto trace = core::traceFromFile(
        core::quantityKindOf<Voltage>(),
        core::Waveform::Timing{ Time{ -0.001 }, Time{ 1e-06 } },
        path);

    //
    // The kind and the timebase come from the call, not from the file: a
    // column of numbers is what every tool exports, and a header on top of one
    // would be a file format invented here.
    //
    EXPECT_EQ( trace.kind(), core::quantityKindOf<Voltage>());
    EXPECT_DOUBLE_EQ( trace.timing().Origin.value(),    -0.001);
    EXPECT_DOUBLE_EQ( trace.timing().Increment.value(), 1e-06);

    ASSERT_EQ( trace.size(), 3u);
    EXPECT_DOUBLE_EQ( trace.minimum<Voltage>().value(), 4.6);
}

//
// The claim that makes one reader rather than two worth having: the file a
// stimulus line names and the file a caller hands traceFromFile are read the
// same way, so a capture can move between the two without meaning anything
// different in either.
//
TEST( CoreStimulus, ASampleFileMeansTheSameThingFromEitherDirection)
{
    const TemporaryDirectory  dir{ "thorium-samples-agree" };

    const auto path = dir.write( "dip.samples", "5.0\n4.6\n4.8\n");

    auto bank = armed( "Osc1.Channel3 = trace( V, -0.001 s, 1e-06 s, \"dip.samples\")\n", dir.Path);

    const auto parsed = bank.active().fetchTrace( "Osc1.Channel3", "Osc1", []{ return core::Waveform{}; });

    EXPECT_EQ( parsed,
               core::traceFromFile( core::quantityKindOf<Voltage>(),
                                    core::Waveform::Timing{ Time{ -0.001 }, Time{ 1e-06 } },
                                    path));
}

TEST( CoreStimulus, AMissingSampleFileNamesItself)
{
    const TemporaryDirectory  dir{ "thorium-samples-missing" };

    const auto path = dir.Path / "absent.samples";

    const auto message = messageFrom( [&]{ (void) core::readSamples( path); });

    EXPECT_TRUE( mentions( message, path.string())) << message;
}

TEST( CoreStimulus, SomethingThatIsNotANumberNamesBothTheTokenAndTheFile)
{
    const TemporaryDirectory  dir{ "thorium-samples-malformed" };

    //
    // The file, not only the token, because these numbers live in a different
    // file from the trace() line that names them -- a stimulus line number on
    // its own points at the wrong file, and a traceFromFile caller has no line
    // number at all.
    //
    const auto path = dir.write( "dip.samples", "5.0 4.6 nope\n");

    const auto message = messageFrom( [&]{ (void) core::readSamples( path); });

    EXPECT_TRUE( mentions( message, "nope"))         << message;
    EXPECT_TRUE( mentions( message, path.string()))  << message;
}

TEST( CoreStimulus, AnEmptySampleFileIsRejectedRatherThanArmingATraceOfNothing)
{
    const TemporaryDirectory  dir{ "thorium-samples-empty" };

    //
    // The failed export -- a truncated capture, or a path that happened to name
    // an empty file. Left alone it arms a trace whose every reduction throws
    // "there is no minimum of nothing" somewhere later, pointing nowhere near
    // the file, so it is rejected here where the file can still be named. The
    // inline [ ] form is untouched: an author who writes it has said so.
    //
    const auto path = dir.write( "empty.samples", "\n  \n");

    const auto message = messageFrom( [&]{ (void) core::readSamples( path); });

    EXPECT_TRUE( mentions( message, path.string())) << message;

    EXPECT_THROW( (void) core::traceFromFile( core::quantityKindOf<Voltage>(),
                                              core::Waveform::Timing{ Time{ 0.0 }, Time{ 1e-06 } },
                                              path),
                  std::runtime_error);
}

//
// And the same rejection through the parser, since it is the same reader --
// a stimulus file naming a truncated export fails on the line that named it
// rather than three scripts later.
//
TEST( CoreStimulus, AStimulusLineNamingAnEmptySampleFileFailsOnThatLine)
{
    const TemporaryDirectory  dir{ "thorium-samples-empty-line" };

    dir.write( "empty.samples", "");

    core::SessionBank  bank;
    std::istringstream in( "Osc1.Channel3 = trace( V, 0 s, 1e-06 s, \"empty.samples\")\n");

    EXPECT_THROW( core::injectStimulus( bank, in, dir.Path), std::runtime_error);
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
