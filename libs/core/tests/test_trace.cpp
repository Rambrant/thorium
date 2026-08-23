#include "core/trace.hpp"

#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/journal.hpp"
#include "core/recording.hpp"
#include "core/session.hpp"
#include "core/waveform.hpp"

using namespace core::quantities;

//
// A minimal instrument holding a captured record, satisfying exactly what
// core::FetchEngine needs: a builder with a config(), an ADL fetchDriver over
// it, and a describeConfig for the log. hal::DSO8064A happens to satisfy the
// same shape; core knows nothing about it.
//
// Two channels, so the qualifier claim below is about something real.
// Deliberately not called an oscilloscope, for the reason test_acquire.cpp's
// mock is not one: "hand back the record you captured" is equally a transient
// recorder or a datalogger with a memory behind it.
//
namespace mock
{
    class Digitizer;

    struct RecordConfig
    {
        Digitizer &  Instrument;
        unsigned     Channel;
    };

    class RecordBuilder
    {
        public:
            using Config = RecordConfig;

            RecordBuilder( Digitizer & instrument, const unsigned channel) : mConfig{ instrument, channel } {}

            [[nodiscard]] auto config() const -> const Config & { return mConfig; }

        private:
            Config mConfig;
    };

    class Digitizer
    {
        public:
            [[nodiscard]] auto record( const unsigned channel) -> RecordBuilder { return RecordBuilder{ *this, channel }; }

            auto setTrace( const unsigned channel, core::Waveform trace) -> void
            {
                mTraces[ channel] = std::move( trace);
            }

            [[nodiscard]]
            auto trace( const unsigned channel) -> core::Waveform
            {
                ++mFetchCount;

                return mTraces[ channel];
            }

            [[nodiscard]] auto fetchCount() const -> int { return mFetchCount; }

        private:
            std::unordered_map<unsigned, core::Waveform>  mTraces;
            int                                           mFetchCount{ 0 };
    };

    [[nodiscard]]
    inline auto fetchDriver( const RecordConfig & config) -> core::Waveform
    {
        return config.Instrument.trace( config.Channel);
    }

    [[nodiscard]]
    inline auto traceQualifier( const RecordConfig & config) -> std::string
    {
        return "Channel" + std::to_string( config.Channel);
    }

    inline auto describeConfig( const RecordConfig & ) -> core::SourceDescription
    {
        return core::SourceDescription{ "Rec1", "record" };
    }

    //
    // A second instrument with no traceQualifier at all -- a single-record
    // device, which must not have to write a function saying it has nothing to
    // qualify.
    //
    class Logger;

    struct LogConfig
    {
        Logger & Instrument;
    };

    class LogBuilder
    {
        public:
            using Config = LogConfig;

            explicit LogBuilder( Logger & instrument) : mConfig{ instrument } {}

            [[nodiscard]] auto config() const -> const Config & { return mConfig; }

        private:
            Config mConfig;
    };

    class Logger
    {
        public:
            [[nodiscard]] auto record() -> LogBuilder { return LogBuilder{ *this }; }
    };

    [[nodiscard]]
    inline auto fetchDriver( const LogConfig &) -> core::Waveform
    {
        return core::Waveform{};
    }

    inline auto describeConfig( const LogConfig &) -> core::SourceDescription
    {
        return core::SourceDescription{ "Log1", "record" };
    }
} // namespace mock

namespace
{
    auto traceOf( const double level) -> core::Waveform
    {
        return core::Waveform{
            core::quantityKindOf<Voltage>(),
            core::Waveform::Timing{ Time{ 0.0 }, Time{ 1e-06 } },
            std::vector<double>( 8, level) };
    }

    struct TraceFixture : ::testing::Test
    {
        mock::Digitizer    rec1;
        mock::Logger       log1;
        core::SessionBank  sessions;
        core::FetchEngine  Fetch{ sessions };
    };
} // namespace

TEST_F( TraceFixture, AFetchTakesTheRecordOffTheInstrument)
{
    rec1.setTrace( 3, traceOf( 5.0));

    EXPECT_EQ( Fetch( rec1.record( 3)), traceOf( 5.0));
    EXPECT_EQ( rec1.fetchCount(), 1);
}

//
// The claim the qualifier exists for. One slot for every channel would let a
// test inject a channel-1 trace and have a channel-3 Fetch take it, and would
// replay a two-channel capture into itself.
//
TEST_F( TraceFixture, EachChannelHasItsOwnSessionSlot)
{
    Fetch.inject( "Rec1.Channel1", traceOf( 3.3));
    Fetch.inject( "Rec1.Channel3", traceOf( 5.0));

    EXPECT_EQ( Fetch( rec1.record( 1)), traceOf( 3.3));
    EXPECT_EQ( Fetch( rec1.record( 3)), traceOf( 5.0));

    // Neither went near the instrument.
    EXPECT_EQ( rec1.fetchCount(), 0);
}

TEST_F( TraceFixture, AnInstrumentWithNothingToQualifyFilesUnderTrace)
{
    //
    // No traceQualifier beside mock::LogConfig, so the key is the default --
    // a single-record device should not have to write a function to say it has
    // one record.
    //
    Fetch.inject( "Log1.Trace", traceOf( 1.5));

    EXPECT_EQ( Fetch( log1.record()), traceOf( 1.5));
}

TEST_F( TraceFixture, ASequenceOfTracesIsTakenInOrder)
{
    Fetch.inject( "Rec1.Channel1", { traceOf( 5.0), traceOf( 4.6) });

    EXPECT_EQ( Fetch( rec1.record( 1)), traceOf( 5.0));
    EXPECT_EQ( Fetch( rec1.record( 1)), traceOf( 4.6));

    // Running past the end is a hard error, as it is on the other three seams.
    EXPECT_THROW( (void) Fetch( rec1.record( 1)), std::runtime_error);
}

TEST_F( TraceFixture, ATraceGoesIntoTheSameOrderedRecordingAsEverythingElse)
{
    sessions.startRecording();

    rec1.setTrace( 3, traceOf( 5.0));

    (void) sessions.active().fetch( "Output5V", "Dmm1", core::quantityKindOf<Voltage>(),
                                    []{ return core::QuantityVariant{ Voltage{ 5.02 } }; });
    (void) Fetch( rec1.record( 3));

    ASSERT_EQ( sessions.samples().size(), 2u);

    // Order is what makes a replay faithful -- the trace is the second thing
    // this run observed, and comes back as the second thing.
    EXPECT_EQ( sessions.samples()[ 0].mSequence, 0u);
    EXPECT_EQ( sessions.samples()[ 1].mSequence, 1u);
    EXPECT_EQ( sessions.samples()[ 1].mPointName, "Rec1.Channel3");
    EXPECT_TRUE( std::holds_alternative<core::Waveform>( sessions.samples()[ 1].mValue));
}

//
// A trace's log line summarises; it never carries the samples. See
// core::describeValue for a Waveform on why this is a rule rather than a bound.
//
TEST_F( TraceFixture, TheLogLineSummarisesTheTrace)
{
    class Capturing : public core::IJournalSink
    {
        public:
            auto onEvent( const core::JournalEvent & event) -> void override { Events.push_back( event); }

            std::vector<core::JournalEvent> Events;
    };

    Capturing sink;

    core::journal().clearSinks();
    core::journal().add( sink);
    core::journal().begin( core::RunInfo{});

    rec1.setTrace( 2, traceOf( 5.0));

    (void) Fetch( rec1.record( 2));

    core::journal().clearSinks();

    ASSERT_EQ( sink.Events.size(), 1u);
    EXPECT_EQ( sink.Events.front().Method,  core::Verb::Fetch);
    EXPECT_EQ( sink.Events.front().Subject, "Rec1.Channel2");
    EXPECT_NE( sink.Events.front().Value.find( "8 pts"), std::string::npos) << sink.Events.front().Value;
}
