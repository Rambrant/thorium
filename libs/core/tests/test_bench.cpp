#include "core/bench.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/acquire.hpp"
#include "core/journal.hpp"
#include "core/route.hpp"
#include "core/session.hpp"
#include "core/source.hpp"
#include "core/transfer.hpp"

//
// The stimulus half of "where did this run's behaviour actually come from" --
// core/bench.hpp.
//
// The observing verbs have been answerable since core::ISession existed: a
// replayed Measure never calls its driver, because the driver call lives inside
// a callback a scripted session does not invoke. The instructing verbs had no
// such answer and called their drivers unconditionally, so a replayed run took
// its readings from a file and then energised rails for real. These tests are
// that hole, closed: every verb that instructs, asserted not to.
//

namespace mock
{
    //
    // One instrument answering to every stimulus customization point at once,
    // counting each. Deliberately one type rather than seven: what is being
    // asserted is a property of the *engines*, identically, and seven mocks
    // would make seven chances to write the assertion slightly differently.
    //
    class Instrument
    {
        public:
            int Applied{ 0 };
            int Removed{ 0 };
            int SetUp{ 0 };
            int Written{ 0 };
            int Armed{ 0 };
            int Connected{ 0 };
            int Disconnected{ 0 };

            [[nodiscard]]
            auto total() const -> int
            {
                return Applied + Removed + SetUp + Written + Armed + Connected + Disconnected;
            }
    };

    struct Config
    {
        Instrument & Target;
    };

    class Builder
    {
        public:
            using Config = mock::Config;

            explicit Builder( Instrument & target) : mConfig{ target } {}

            [[nodiscard]] auto config() const -> const Config & { return mConfig; }

        private:
            Config mConfig;
    };

    inline auto applyDriver(  const Config & config) -> void { ++config.Target.Applied; }
    inline auto removeDriver( const Config & config) -> void { ++config.Target.Removed; }
    inline auto setupDriver(  const Config & config) -> void { ++config.Target.SetUp;   }
    inline auto armDriver(    const Config & config) -> void { ++config.Target.Armed;   }

    inline auto writeDriver( const Config & config, const core::Bytes &) -> void { ++config.Target.Written; }

    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    auto connectDriver( FabricT &, const InstrumentWiringT &, const ConnectorWiringT &, const Config & config) -> void
    {
        ++config.Target.Connected;
    }

    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    auto disconnectDriver( FabricT &, const InstrumentWiringT &, const ConnectorWiringT &, const Config & config) -> void
    {
        ++config.Target.Disconnected;
    }

    inline auto describeConfig( const Config &) -> core::SourceDescription
    {
        return core::SourceDescription{ "Mock1", "everything" };
    }

    //
    // Stand-ins for the three rig facts Connect/Disconnect are generic over.
    // Empty, because the mock drivers above never look at them -- what routing
    // means is the driver's business, and this file is about whether the driver
    // is reached at all.
    //
    struct Fabric {};
    struct Wiring {};
} // namespace mock

namespace
{
    class CapturingSink : public core::IJournalSink
    {
        public:
            auto onEvent( const core::JournalEvent & event) -> void override { Events.push_back( event); }

            std::vector<core::JournalEvent> Events;
    };

    //
    // The bench is process-wide (see core/bench.hpp on why), so every test here
    // has to leave it attached -- otherwise one test's detachment silences the
    // next test's drivers, and the whole file starts passing for the wrong
    // reason.
    //
    class BenchTest : public ::testing::Test
    {
        protected:
            auto SetUp() -> void override
            {
                core::journal().clearSinks();
                core::journal().add( mSink);
                core::journal().begin( core::RunInfo{});
            }

            auto TearDown() -> void override
            {
                core::bench().attach();
                core::journal().clearSinks();
            }

            //
            // Every instructing verb, once. Whether the mock counted anything
            // afterwards is the whole of what these tests ask.
            //
            auto instructEverything() -> void
            {
                core::ApplyEngine  Apply;
                core::SetupEngine  Setup;
                core::RemoveEngine Remove;
                core::WriteEngine  Write;
                core::ArmEngine    Arm;

                core::ConnectEngine<mock::Fabric, mock::Wiring, mock::Wiring>    Connect{ mFabric, mWiring, mWiring };
                core::DisconnectEngine<mock::Fabric, mock::Wiring, mock::Wiring> Disconnect{ mFabric, mWiring, mWiring };

                const auto builder = mock::Builder{ mInstrument };

                Connect(    builder);
                Setup(      builder);
                Apply(      builder);
                Write(      builder, core::Bytes( "RD 30\r"));
                Arm(        builder);
                Remove(     builder);
                Disconnect( builder);
            }

            mock::Instrument  mInstrument;
            mock::Fabric      mFabric;
            mock::Wiring      mWiring;
            CapturingSink     mSink;
    };
} // namespace

TEST_F( BenchTest, ABenchIsAttachedUntilSomebodySaysOtherwise)
{
    //
    // The default matters more than it looks. A default of detached would make
    // every driver test in this repo pass by never calling anything.
    //
    EXPECT_TRUE( core::bench().isAttached());

    instructEverything();

    EXPECT_EQ( mInstrument.Applied,      1);
    EXPECT_EQ( mInstrument.Removed,      1);
    EXPECT_EQ( mInstrument.SetUp,        1);
    EXPECT_EQ( mInstrument.Written,      1);
    EXPECT_EQ( mInstrument.Armed,        1);
    EXPECT_EQ( mInstrument.Connected,    1);
    EXPECT_EQ( mInstrument.Disconnected, 1);
}

//
// The claim the whole file exists for, and the one that was false before: a run
// with no bench instructs nothing at all.
//
TEST_F( BenchTest, ADetachedBenchReachesNoDriverAtAll)
{
    core::bench().detach();

    instructEverything();

    EXPECT_EQ( mInstrument.total(), 0);
}

//
// And the log still shows the sequence, because a routing step omitted for
// brevity is exactly the one that explains a failed reading -- it says the
// instruction went nowhere rather than pretending it did, or vanishing.
//
TEST_F( BenchTest, EveryInstructionIsStillLoggedAndSaysItWentNowhere)
{
    core::bench().detach();

    instructEverything();

    ASSERT_EQ( mSink.Events.size(), 7u);

    for( const auto & event : mSink.Events)
    {
        EXPECT_EQ( event.Detail, core::kDetachedDetail)
            << "unmarked " << core::to_string( event.Method) << " event";
    }
}

TEST_F( BenchTest, AnAttachedRunsLogIsUnchanged)
{
    instructEverything();

    ASSERT_EQ( mSink.Events.size(), 7u);

    for( const auto & event : mSink.Events)
    {
        EXPECT_NE( event.Detail, core::kDetachedDetail);
    }

    // Write's own Detail survives rather than being replaced by the marker.
    const auto written = std::ranges::find_if( mSink.Events,
        []( const auto & event) { return event.Method == core::Verb::Write; });

    ASSERT_NE( written, mSink.Events.end());
    EXPECT_EQ( written->Detail, "sent");
}

//
// Detachment is NOT the same fact as injecting readings, and must not become
// one -- a script unit test injects its values and then asserts on the
// simulated instrument state an Apply produced. Deriving one from the other
// would silence exactly the calls those tests exist to observe.
//
//
// The last path by which a reading could still reach an instrument in a run
// that instructs nothing. Closed by refusing rather than substituting: there is
// no honest value to hand back, and a zero would be a number no instrument
// produced sitting beside a criterion that then passes on it.
//
TEST_F( BenchTest, ALiveReadingWithNoBenchIsRefusedRatherThanTaken)
{
    core::LiveSession live;

    core::bench().detach();

    auto reached = false;

    try
    {
        (void) live.fetch( "Output5V", "Dmm1", core::quantityKindOf<core::quantities::Voltage>(),
                           [&]() -> core::QuantityVariant { reached = true; return core::QuantityVariant{}; });

        ADD_FAILURE() << "a live reading with no bench attached should not be taken";
    }
    catch( const std::runtime_error & error)
    {
        EXPECT_NE( std::string( error.what()).find( "Output5V"), std::string::npos);
    }

    EXPECT_FALSE( reached) << "the driver callback must not have run";
}

TEST_F( BenchTest, InjectingReadingsDoesNotDetachTheBench)
{
    core::SessionBank bank;

    bank.inject( "Output5V", core::QuantityVariant{ core::quantities::Voltage{ 5.02 } });

    EXPECT_TRUE( core::bench().isAttached());

    instructEverything();

    EXPECT_EQ( mInstrument.Applied, 1);
}
