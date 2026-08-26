#include "core/recording.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "core/bytes.hpp"
#include "core/session.hpp"
#include "core/waveform.hpp"

//
// Heavy payloads and the sidecar directory they live in -- core::RecordingWriter,
// core::sidecarDirectoryFor, and what readRecording does with a reference.
//
// The claims worth pinning here are the ones the format was chosen for and
// could quietly lose: that a row still differs when its payload does (which is
// why the blobs are named after their contents), that the same payload observed
// twice is stored once, and that a reference is never resolved anywhere but the
// recording's own directory.
//

using core::Bytes;

namespace
{
    //
    // Bigger than core::kMaxInlinePayload, and made of something that is not a
    // single repeated octet so that two "large" payloads in one test are
    // genuinely different sequences rather than different lengths.
    //
    auto largePayload( const unsigned char seed) -> Bytes
    {
        std::vector<std::byte> data;

        data.reserve( core::kMaxInlinePayload * 4);

        for( std::size_t i = 0; i < core::kMaxInlinePayload * 4; ++i)
        {
            data.push_back( static_cast<std::byte>( ( i * 31 + seed) & 0xFF));
        }

        return Bytes( std::move( data));
    }

    auto payloadRow( const std::uint64_t sequence, const Bytes & payload) -> core::RecordedSample
    {
        return core::RecordedSample{
            sequence, 1000 + static_cast<std::int64_t>( sequence), "Console", "Ser1.Data", "Ser1", payload };
    }

    //
    // A directory of its own per test, removed afterwards -- these tests write
    // real files, and a blob is named after its content, so two tests sharing a
    // directory would silently share blobs as well.
    //
    class RecordingFiles : public ::testing::Test
    {
        protected:
            auto SetUp() -> void override
            {
                mRoot = std::filesystem::temp_directory_path() /
                        ( "thorium-recording-" + std::string( ::testing::UnitTest::GetInstance()->current_test_info()->name()));

                std::filesystem::remove_all( mRoot);
                std::filesystem::create_directories( mRoot);
            }

            auto TearDown() -> void override
            {
                std::filesystem::remove_all( mRoot);
            }

            [[nodiscard]]
            auto recordingPath() const -> std::filesystem::path
            {
                return mRoot / "readings.tsv";
            }

            [[nodiscard]]
            auto sidecarPath() const -> std::filesystem::path
            {
                return core::sidecarDirectoryFor( recordingPath());
            }

            //
            // The value column of the one row in text -- what a reader diffing
            // two recordings actually compares.
            //
            [[nodiscard]]
            static auto valueColumn( const std::string & row) -> std::string
            {
                const auto lastTab = row.find_last_of( '\t');

                auto value = row.substr( lastTab + 1);

                if( !value.empty() && value.back() == '\n')
                {
                    value.pop_back();
                }

                return value;
            }

            std::filesystem::path mRoot;
    };
} // namespace

TEST( CoreRecordingSidecar, TheSidecarDirectoryIsTheRecordingsOwnNameWithSuffix)
{
    //
    // Appended rather than substituted for the extension, so that two
    // recordings in one directory can never share a blob store -- see
    // core::sidecarDirectoryFor.
    //
    EXPECT_EQ( core::sidecarDirectoryFor( "readings.tsv"), std::filesystem::path( "readings.tsv.d"));
    EXPECT_EQ( core::sidecarDirectoryFor( "readings.csv"), std::filesystem::path( "readings.csv.d"));
    EXPECT_NE( core::sidecarDirectoryFor( "readings.tsv"), core::sidecarDirectoryFor( "readings.csv"));
}

TEST_F( RecordingFiles, ASmallPayloadStaysInTheFileAndLeavesNoDirectory)
{
    //
    // The bound has to be invisible for everything a console protocol
    // exchanges: a run that records nothing heavy leaves one file, exactly as
    // it did before sidecars existed.
    //
    std::ostringstream out;

    core::writeRecording( out, { payloadRow( 0, Bytes( "ACK\r")) }, sidecarPath());

    EXPECT_EQ( valueColumn( out.str()), "41434B0D");
    EXPECT_FALSE( std::filesystem::exists( sidecarPath()));
}

TEST_F( RecordingFiles, ALargePayloadLeavesTheFileAndTheRowRefersToIt)
{
    const auto payload = largePayload( 1);

    std::ostringstream out;

    core::writeRecording( out, { payloadRow( 0, payload) }, sidecarPath());

    const auto reference = valueColumn( out.str());

    ASSERT_FALSE( reference.empty());
    EXPECT_EQ( reference.front(), core::kSidecarMarker);

    //
    // The blob is the payload and nothing else -- no header, no encoding. Half
    // the reason for having one is that ordinary tools can open it.
    //
    const auto blob = sidecarPath() / reference.substr( 1);

    ASSERT_TRUE( std::filesystem::exists( blob));
    EXPECT_EQ( std::filesystem::file_size( blob), payload.size());
}

TEST_F( RecordingFiles, ALargePayloadRoundTripsThroughItsSidecar)
{
    const auto payload = largePayload( 2);

    std::ostringstream out;

    core::writeRecording( out, { payloadRow( 0, payload) }, sidecarPath());

    std::istringstream in( out.str());

    const auto readBack = core::readRecording( in, sidecarPath());

    ASSERT_EQ( readBack.size(), 1u);
    EXPECT_EQ( std::get<Bytes>( readBack.front().mValue), payload);
}

//
// The property the whole naming scheme exists for. A reference keyed on the
// row's sequence number would be byte-identical between a run whose DUT
// answered correctly and one whose DUT did not, and diffing two recordings --
// which is what the flat TSV was chosen for -- would show nothing at all.
//
TEST_F( RecordingFiles, TwoRunsWithDifferentPayloadsProduceDifferentRows)
{
    std::ostringstream first;
    std::ostringstream second;

    core::writeRecording( first,  { payloadRow( 0, largePayload( 1)) }, sidecarPath());
    core::writeRecording( second, { payloadRow( 0, largePayload( 2)) }, sidecarPath());

    EXPECT_NE( valueColumn( first.str()), valueColumn( second.str()));
}

TEST_F( RecordingFiles, TheSamePayloadObservedTwiceIsStoredOnce)
{
    const auto payload = largePayload( 3);

    std::ostringstream out;

    core::writeRecording( out, { payloadRow( 0, payload), payloadRow( 1, payload) }, sidecarPath());

    const auto files = std::distance( std::filesystem::directory_iterator( sidecarPath()),
                                      std::filesystem::directory_iterator{});

    EXPECT_EQ( files, 1);

    //
    // And both rows still replay -- dedup is a fact about storage, not about
    // how many times the run observed the payload.
    //
    std::istringstream in( out.str());

    const auto readBack = core::readRecording( in, sidecarPath());

    ASSERT_EQ( readBack.size(), 2u);
    EXPECT_EQ( std::get<Bytes>( readBack[ 0].mValue), payload);
    EXPECT_EQ( std::get<Bytes>( readBack[ 1].mValue), payload);
}

//
// The one case that must never be written through. Two different payloads on
// one name would replay the wrong octets, silently -- so a writer that finds
// the name taken compares before reusing it, and refuses rather than
// overwriting.
//
// Provoked by corrupting the blob rather than by finding a real FNV-1a
// collision: the branch under test is "the name is taken by something that is
// not this payload", and how it came to be taken is not what is being asserted.
//
TEST_F( RecordingFiles, APayloadThatCollidesWithADifferentOneIsRefused)
{
    const auto payload = largePayload( 4);

    std::ostringstream first;

    core::writeRecording( first, { payloadRow( 0, payload) }, sidecarPath());

    const auto blob = sidecarPath() / valueColumn( first.str()).substr( 1);

    {
        std::ofstream corrupt( blob, std::ios::binary | std::ios::trunc);

        corrupt << "not the payload that hashes to this name";
    }

    std::ostringstream second;

    EXPECT_THROW( core::writeRecording( second, { payloadRow( 0, payload) }, sidecarPath()), std::runtime_error);
}

TEST_F( RecordingFiles, AReferenceWithNoSidecarDirectoryIsAHardError)
{
    //
    // Half a recording replayed is a run whose payloads come from somewhere the
    // file does not say, which is worse than not replaying at all.
    //
    std::ostringstream out;

    core::writeRecording( out, { payloadRow( 0, largePayload( 5)) }, sidecarPath());

    std::istringstream in( out.str());

    EXPECT_THROW( (void) core::readRecording( in), std::runtime_error);
}

TEST_F( RecordingFiles, AReferenceIsNeverResolvedOutsideItsOwnDirectory)
{
    //
    // A recording is a file read off disk and mailed around after a failure.
    // A row naming a path is a corrupt row, not an instruction.
    //
    for( const auto * reference : { "../secrets.bin", "/etc/passwd", "sub/dir.bin", "..", "" })
    {
        std::istringstream in( std::string( "0\t0\tConsole\tSer1.Data\tSer1\t<bytes>\t@") + reference + "\n");

        EXPECT_THROW( (void) core::readRecording( in, sidecarPath()), std::runtime_error) << "reference: " << reference;
    }
}

//
// The point of streaming: what has been observed is on disk, so a run killed
// part-way leaves the readings it took rather than an empty file.
//
TEST_F( RecordingFiles, AStreamedRecordingIsOnDiskBeforeTheRunEnds)
{
    core::LiveSession      live;
    core::RecordingSession recorder( live);

    std::ostringstream out;

    core::RecordingWriter writer( out, sidecarPath());

    recorder.streamTo( writer);

    (void) recorder.fetchData( "Ser1.Data", "Ser1", []{ return Bytes( "ACK\r"); });

    EXPECT_FALSE( out.str().empty()) << "the row should be written as it is observed, not at the end";

    //
    // And it is not also being kept. samples() is empty while streaming, which
    // is what core::SessionBank::dump refuses on rather than writing an empty
    // recording over a full one.
    //
    EXPECT_TRUE( recorder.samples().empty());
    EXPECT_TRUE( recorder.isStreaming());
}

TEST_F( RecordingFiles, DumpingAStreamedRecordingIsRefused)
{
    std::ostringstream    rows;
    core::RecordingWriter writer( rows, sidecarPath());
    core::SessionBank     bank;

    bank.startRecording( writer);

    (void) bank.active().fetchData( "Ser1.Data", "Ser1", []{ return Bytes( "ACK\r"); });

    std::ostringstream elsewhere;

    EXPECT_THROW( bank.dump( elsewhere), std::logic_error);
}

//
// End to end, which is the claim that actually matters: a run that observed
// something too large for the file can still be replayed off it.
//
TEST_F( RecordingFiles, AHeavyPayloadRecordedByARunReplaysFromTheFile)
{
    const auto payload = largePayload( 6);

    {
        std::ofstream         file( recordingPath(), std::ios::out | std::ios::trunc);
        core::RecordingWriter writer( file, sidecarPath());
        core::SessionBank     bank;

        bank.startRecording( writer);

        (void) bank.active().fetchData( "Ser1.Data", "Ser1", [&]{ return payload; });
        (void) bank.active().fetchFlag( "Osc1.Acquisition", "Osc1", []{ return true; });

        bank.stopRecording();
    }

    core::SessionBank replayed;

    replayed.load( recordingPath().string());

    EXPECT_EQ( replayed.active().fetchData( "Ser1.Data", "Ser1", []{ return Bytes( "live"); }), payload);
    EXPECT_TRUE( replayed.active().fetchFlag( "Osc1.Acquisition", "Osc1", []{ return false; }));
}

//
// Trace rows -- the fourth kind of observation, and the one that made the
// sidecar necessary. See kTraceKind in core/recording.hpp for the row's shape
// and why the timebase stays in the file while the samples leave it.
//

namespace
{
    auto traceOf( const std::size_t points, const double level) -> core::Waveform
    {
        return core::Waveform{
            core::quantityKindOf<core::quantities::Voltage>(),
            core::Waveform::Timing{ core::quantities::Time{ -0.001 }, core::quantities::Time{ 1e-06 } },
            std::vector<double>( points, level) };
    }

    auto traceRow( const std::uint64_t sequence, const core::Waveform & trace) -> core::RecordedSample
    {
        return core::RecordedSample{ sequence, 1000, "AcDropout", "Osc1.Channel3", "Osc1", trace };
    }
} // namespace

TEST_F( RecordingFiles, AShortTraceStaysInTheFile)
{
    //
    // Same threshold as a payload, and for the same reason -- one rule for
    // "too large to keep in a row", not one per kind of value.
    //
    const auto trace = traceOf( 4, 5.0);

    std::ostringstream out;

    core::writeRecording( out, { traceRow( 0, trace) }, sidecarPath());

    EXPECT_NE( valueColumn( out.str()).front(), core::kSidecarMarker);
    EXPECT_FALSE( std::filesystem::exists( sidecarPath()));

    std::istringstream in( out.str());

    const auto readBack = core::readRecording( in, sidecarPath());

    ASSERT_EQ( readBack.size(), 1u);
    EXPECT_EQ( std::get<core::Waveform>( readBack.front().mValue), trace);
}

TEST_F( RecordingFiles, ALongTraceLeavesTheFileButItsTimebaseStays)
{
    const auto trace = traceOf( 4096, 5.0);

    std::ostringstream out;

    core::writeRecording( out, { traceRow( 0, trace) }, sidecarPath());

    const auto row = out.str();

    EXPECT_EQ( valueColumn( row).front(), core::kSidecarMarker);

    //
    // The point of keeping the timebase in the row: two runs whose captures
    // differ only in their sample rate differ visibly, without opening
    // anything.
    //
    EXPECT_NE( row.find( "Voltage"), std::string::npos) << row;
    EXPECT_NE( row.find( "1e-06"),   std::string::npos) << row;
    EXPECT_NE( row.find( "-0.001"),  std::string::npos) << row;
}

TEST_F( RecordingFiles, ALongTraceRoundTripsSampleForSample)
{
    //
    // Sample for sample, not approximately: the samples are stored as their
    // own bits rather than as text, precisely so that a replayed run reaches
    // the same verdict the recorded one did.
    //
    std::vector<double> samples;

    for( std::size_t i = 0; i < 4096; ++i)
    {
        samples.push_back( 5.0 - 0.4 * std::sin( static_cast<double>( i) / 97.0));
    }

    const auto trace = core::Waveform{
        core::quantityKindOf<core::quantities::Voltage>(),
        core::Waveform::Timing{ core::quantities::Time{ -0.001 }, core::quantities::Time{ 1e-06 } },
        std::move( samples) };

    std::ostringstream out;

    core::writeRecording( out, { traceRow( 0, trace) }, sidecarPath());

    std::istringstream in( out.str());

    const auto readBack = core::readRecording( in, sidecarPath());

    ASSERT_EQ( readBack.size(), 1u);
    EXPECT_EQ( std::get<core::Waveform>( readBack.front().mValue), trace);
}

TEST_F( RecordingFiles, ATraceWhoseSamplesAreNotWholeIsAMalformedRow)
{
    //
    // Nine octets is not a whole number of eight-octet samples, and a row that
    // is a byte short is corrupt rather than a trace with a partial sample on
    // the end.
    //
    std::istringstream in( "0\t0\tAcDropout\tOsc1.Channel3\tOsc1\t<trace>\tVoltage\t0\t1e-06\t000000000000000000\n");

    EXPECT_THROW( (void) core::readRecording( in, sidecarPath()), std::runtime_error);
}

TEST_F( RecordingFiles, ATraceRowMissingItsTimebaseIsAMalformedRow)
{
    std::istringstream in( "0\t0\tAcDropout\tOsc1.Channel3\tOsc1\t<trace>\tVoltage\n");

    EXPECT_THROW( (void) core::readRecording( in, sidecarPath()), std::runtime_error);
}

//
// The claim the whole of point four rests on: every kind of observation a run
// makes goes into one ordered recording, and comes back out of it.
//
TEST_F( RecordingFiles, AllFourSeamsShareOneRecordingAndReplayFromIt)
{
    const auto trace   = traceOf( 4096, 5.0);
    const auto payload = largePayload( 7);

    {
        std::ofstream         file( recordingPath(), std::ios::out | std::ios::trunc);
        core::RecordingWriter writer( file, sidecarPath());
        core::SessionBank     bank;

        bank.startRecording( writer);

        (void) bank.active().fetch( "Output5V", "Dmm1", core::quantityKindOf<core::quantities::Voltage>(),
                                    []{ return core::QuantityVariant{ core::quantities::Voltage{ 5.02 } }; });
        (void) bank.active().fetchFlag(  "Osc1.Acquisition", "Osc1", []{ return true; });
        (void) bank.active().fetchTrace( "Osc1.Channel3",    "Osc1", [&]{ return trace; });
        (void) bank.active().fetchData(  "Ser1.Data",        "Ser1", [&]{ return payload; });

        bank.stopRecording();
    }

    core::SessionBank replayed;

    replayed.load( recordingPath().string());

    EXPECT_DOUBLE_EQ(
        core::asQuantity<core::quantities::Voltage>(
            replayed.active().fetch( "Output5V", "Dmm1", core::quantityKindOf<core::quantities::Voltage>(),
                                     []{ return core::QuantityVariant{ core::quantities::Voltage{ 0.0 } }; })).value(),
        5.02);

    EXPECT_TRUE( replayed.active().fetchFlag( "Osc1.Acquisition", "Osc1", []{ return false; }));
    EXPECT_EQ(   replayed.active().fetchTrace( "Osc1.Channel3",   "Osc1", []{ return core::Waveform{}; }), trace);
    EXPECT_EQ(   replayed.active().fetchData(  "Ser1.Data",       "Ser1", []{ return Bytes( "live"); }),   payload);
}

//
// ---------------------------------------------------------------------
// The test column
// ---------------------------------------------------------------------
//

//
// Which test a reading belongs to is part of the row, so it survives the round
// trip like any other column -- and so a row stays independently meaningful
// however it is filtered or sorted. See core::RecordedSample.
//
TEST( RecordingTestColumn, ATestIdRoundTrips)
{
    const std::vector<core::RecordedSample> samples{
        { 0, 1000, "SupplyRail", "Output5V", "Dmm1",
          core::QuantityVariant{ core::quantities::Voltage{ 5.0 } } },
        { 1, 1001, std::string( core::kRunScope), "DcP1.Voltage", "DcP1",
          core::QuantityVariant{ core::quantities::Voltage{ 28.0 } } }
    };

    std::ostringstream out;
    core::writeRecording( out, samples);

    std::istringstream in( out.str());
    const auto         readBack = core::readRecording( in);

    ASSERT_EQ( readBack.size(), 2u);
    EXPECT_EQ( readBack[ 0].mTestId, "SupplyRail");
    EXPECT_EQ( readBack[ 1].mTestId, core::kRunScope);
}

//
// The test column sits between the timestamp and the point, so the row reads
// left to right as when / which test / which point / which instrument. Asserted
// on the text because this is the format, and a reader outside this repo parses
// by position.
//
TEST( RecordingTestColumn, TheTestSitsBetweenTheTimestampAndThePoint)
{
    const std::vector<core::RecordedSample> samples{
        { 0, 1000, "SupplyRail", "Output5V", "Dmm1",
          core::QuantityVariant{ core::quantities::Voltage{ 5.0 } } }
    };

    std::ostringstream out;
    core::writeRecording( out, samples);

    EXPECT_EQ( out.str(), "0\t1000\tSupplyRail\tOutput5V\tDmm1\tVoltage\t5\n");
}

//
// Every column but the value has to carry something. A blank test column would
// otherwise reach a selected replay as an id no test has and be dropped from a
// selection it should have been exempt from -- see core::kRunScope.
//
TEST( RecordingTestColumn, ARowWithNoTestIdIsMalformed)
{
    std::istringstream in( "0\t0\t\tOutput5V\tDmm1\tVoltage\t5.0\n");

    EXPECT_THROW( (void)core::readRecording( in), std::runtime_error);
}

//
// ---------------------------------------------------------------------
// Numbers are parsed strictly
// ---------------------------------------------------------------------
//

//
// The whole field has to be the number. std::stod, which the reader used to
// use, takes the leading number and discards the rest without a word -- so a
// row of the wrong width, landing in the wrong columns, was read as a plausible
// reading instead of being rejected. That is the one way this format can be
// read wrong rather than refused.
//
TEST( RecordingNumbers, AValueWithTrailingRubbishIsMalformed)
{
    std::istringstream in( "0\t0\tSupplyRail\tOutput5V\tDmm1\tVoltage\t5.021rubbish\n");

    EXPECT_THROW( (void)core::readRecording( in), std::runtime_error);
}

//
// The same hole in the integral columns: std::stoull( "0abc") is 0, silently.
//
TEST( RecordingNumbers, ASequenceNumberWithTrailingRubbishIsMalformed)
{
    std::istringstream in( "0abc\t0\tSupplyRail\tOutput5V\tDmm1\tVoltage\t5.0\n");

    EXPECT_THROW( (void)core::readRecording( in), std::runtime_error);
}

//
// The concrete case: a row from the six-column format this replaced. Its unit
// column lands in the kind column -- and "Voltage" is a real QuantityKind -- so
// only the strict value parse stands between it and a trace being read back as
// a plausible voltage at the wrong point.
//
TEST( RecordingNumbers, AnOldWidthTraceRowIsRefusedRatherThanReadAsAVoltage)
{
    std::istringstream in( "0\t0\tOsc1.Channel3\tOsc1\t<trace>\tVoltage\t0\t1e-06\t000000000000000000\n");

    EXPECT_THROW( (void)core::readRecording( in), std::runtime_error);
}

//
// ---------------------------------------------------------------------
// The selection header
// ---------------------------------------------------------------------
//

TEST( RecordingSelectionHeader, ASelectionIsWrittenAsAComment)
{
    std::ostringstream out;

    core::writeSelectionHeader( out, { "SupplyRail", "StatusRegister" });

    EXPECT_EQ( out.str(), "# select=SupplyRail,StatusRegister\n");
}

//
// A run given no --select says so, rather than leaving the line off: absence
// then means "not a file this framework wrote" instead of being a third state
// to interpret. See core::kEverySelection.
//
TEST( RecordingSelectionHeader, NoSelectionIsWrittenAsEveryTest)
{
    std::ostringstream out;

    core::writeSelectionHeader( out, {});

    EXPECT_EQ( out.str(), "# select=<all>\n");
}

//
// It is a comment, so the reader skips it like any other -- the header is
// provenance for a person and is never read back. See core::kCommentMarker on
// why that line is worth holding.
//
TEST( RecordingSelectionHeader, TheHeaderIsSkippedOnTheWayBackIn)
{
    std::ostringstream out;

    core::writeSelectionHeader( out, { "SupplyRail" });

    core::RecordingWriter writer( out);

    writer.write( core::RecordedSample{
        0, 1000, "SupplyRail", "Output5V", "Dmm1",
        core::QuantityVariant{ core::quantities::Voltage{ 5.0 } } });

    std::istringstream in( out.str());
    const auto         readBack = core::readRecording( in);

    ASSERT_EQ( readBack.size(), 1u);
    EXPECT_EQ( readBack[ 0].mTestId, "SupplyRail");
}
