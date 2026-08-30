#include "core/verbs/interlock.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/session/bench.hpp"
#include "core/journal/journal.hpp"
#include "core/verbs/route.hpp"

//
// The framework half of the electrical interlock: the two message builders, the
// predicate that decides which readings need a dead node, and the ADL probe
// Connect/Disconnect ask a source config through.
//
// The rig half -- whether this bench's DcP3 is actually live, and whether a
// real Measure at a real landing pin is refused -- is rig/tests/test_interlock.cpp,
// which is where the instrument globals and the SOURCE_WIRING table are. This
// file deliberately has neither: everything here is about the mechanism, and a
// mock source is enough to exercise all of it.
//
//
// Unnamed namespace, so this file's mock is this file's own -- see
// test_bench.cpp's mock for why that is a correctness requirement in this
// binary rather than a matter of taste.
//
namespace
{
namespace mock
{
    //
    // A source config in the shape core::ConnectEngine/DisconnectEngine want:
    // an applyDriver (so it counts as something Apply can energise, which is
    // what makes isEnergised mandatory -- see core::detail::energisedNow), a
    // connectDriver/disconnectDriver pair, and a describeConfig for the log.
    //
    // Deliberately not shared with
    // framework/core/tests/session/test_bench.cpp's mock of the same shape.
    // That one is about whether a driver is reached at all and counts calls;
    // this one is about the state a relay moved into, so its interesting
    // member is a settable flag rather than a tally, and folding the two would
    // leave each test file reading past the other's fields.
    //
    struct Supply
    {
        bool Energised{ false };
    };

    struct Config
    {
        Supply & Target;
    };

    class Builder
    {
        public:
            explicit Builder( Supply & target) : mConfig{ target } {}

            [[nodiscard]] auto config() const -> const Config & { return mConfig; }

        private:
            Config mConfig;
    };

    inline auto applyDriver(  const Config & config) -> void { config.Target.Energised = true;  }
    inline auto removeDriver( const Config & config) -> void { config.Target.Energised = false; }

    inline auto isEnergised( const Config & config) -> bool { return config.Target.Energised; }

    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    auto connectDriver( FabricT &, const InstrumentWiringT &, const ConnectorWiringT &, const Config &) -> void {}

    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    auto disconnectDriver( FabricT &, const InstrumentWiringT &, const ConnectorWiringT &, const Config &) -> void {}

    inline auto describeConfig( const Config &) -> core::SourceDescription
    {
        return core::SourceDescription{ "Mock1", "everything" };
    }

    struct Fabric {};
    struct Wiring {};

    //
    // A config with nothing to energise -- a serial interface's shape (see
    // hal::Racal1260Config, which has a setupDriver and no applyDriver). It
    // deliberately has no isEnergised either, and the point of it being here
    // is that this compiles: core::detail::energisedNow only demands the member
    // of a config Apply could energise.
    //
    struct PassiveConfig {};

    class PassiveBuilder
    {
        public:
            [[nodiscard]] auto config() const -> const PassiveConfig & { return mConfig; }

        private:
            PassiveConfig mConfig;
    };

    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    auto connectDriver( FabricT &, const InstrumentWiringT &, const ConnectorWiringT &, const PassiveConfig &) -> void {}

    inline auto describeConfig( const PassiveConfig &) -> core::SourceDescription
    {
        return core::SourceDescription{ "Ser1", "" };
    }
} // namespace mock
} // namespace

namespace
{
    class CapturingSink : public core::IJournalSink
    {
        public:
            auto onEvent( const core::JournalEvent & event) -> void override { Events.push_back( event); }

            std::vector<core::JournalEvent> Events;
    };

    class InterlockTest : public ::testing::Test
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
                // The bench is process-wide (see core/session/bench.hpp), so a test
                // that detached it has to put it back or the next file in this
                // binary silently stops reaching its drivers.
                core::bench().attach();
                core::journal().clearSinks();
            }

            [[nodiscard]]
            auto onlyDetail() const -> std::string
            {
                EXPECT_EQ( mSink.Events.size(), 1u);

                return mSink.Events.empty() ? std::string{} : mSink.Events.front().Detail;
            }

            mock::Supply  mSupply;
            mock::Fabric  mFabric;
            mock::Wiring  mWiring;
            CapturingSink mSink;

            core::ConnectEngine<mock::Fabric, mock::Wiring, mock::Wiring>    mConnect{ mFabric, mWiring, mWiring };
            core::DisconnectEngine<mock::Fabric, mock::Wiring, mock::Wiring> mDisconnect{ mFabric, mWiring, mWiring };
    };
} // namespace

// ---------------------------------------------------------------------------
// The requirement itself, in both directions
// ---------------------------------------------------------------------------

//
// core::detail::energisedNow static_asserts the second of these for anything
// satisfying the first. That assertion can only be demonstrated by breaking a
// build, so what is checked here instead is the pair of concepts it is written
// in terms of -- the same argument rig/tests/test_safing.cpp makes about
// hal::SafeableInstrument, and for the same reason: a guard nobody can test is
// a guard nobody knows is still wired up.
//
static_assert( core::EnergisableConfig<     mock::Config>);
static_assert( core::EnergyReportingConfig< mock::Config>);

//
// The direction that actually shows the split is real. A serial interface has
// nothing to energise, so it is held to neither requirement -- and if
// EnergisableConfig ever started matching it, the first assertion here would
// still pass while the *build* broke somewhere in core/verbs/route.hpp instead.
//
static_assert( ! core::EnergisableConfig<     mock::PassiveConfig>);
static_assert( ! core::EnergyReportingConfig< mock::PassiveConfig>);

// ---------------------------------------------------------------------------
// requiresDeadNode
// ---------------------------------------------------------------------------

//
// Asserted at compile time, because that is where it is used: the guard in
// core::MeasureEngine's routed overload is an `if constexpr`, so a change here
// that stopped being a constant expression would be a different kind of
// breakage than a failing runtime check would describe.
//
static_assert(   core::requiresDeadNode( core::QuantityKind::Current));
static_assert(   core::requiresDeadNode( core::QuantityKind::Resistance));
static_assert( ! core::requiresDeadNode( core::QuantityKind::Voltage));
static_assert( ! core::requiresDeadNode( core::QuantityKind::Frequency));
static_assert( ! core::requiresDeadNode( core::QuantityKind::Power));
static_assert( ! core::requiresDeadNode( core::QuantityKind::Time));

TEST( InterlockPredicate, OnlyTheTwoReadingsThatCannotShareANodeWithASourceQualify)
{
    //
    // The runtime spelling of the static_asserts above, kept so the list reads
    // as a decision about the whole QuantityKind enum rather than as six
    // scattered assertions -- a kind added later should be considered here.
    //
    EXPECT_TRUE(  core::requiresDeadNode( core::QuantityKind::Current));
    EXPECT_TRUE(  core::requiresDeadNode( core::QuantityKind::Resistance));

    EXPECT_FALSE( core::requiresDeadNode( core::QuantityKind::Voltage));
    EXPECT_FALSE( core::requiresDeadNode( core::QuantityKind::Power));
    EXPECT_FALSE( core::requiresDeadNode( core::QuantityKind::ApparentPower));
    EXPECT_FALSE( core::requiresDeadNode( core::QuantityKind::ReactivePower));
    EXPECT_FALSE( core::requiresDeadNode( core::QuantityKind::PowerFactor));
    EXPECT_FALSE( core::requiresDeadNode( core::QuantityKind::Time));
    EXPECT_FALSE( core::requiresDeadNode( core::QuantityKind::Decibel));
    EXPECT_FALSE( core::requiresDeadNode( core::QuantityKind::Frequency));
}

// ---------------------------------------------------------------------------
// The message builders
// ---------------------------------------------------------------------------

TEST( InterlockMessages, HotSwitchDetailSaysWhichHalfOfTheMechanismHappened)
{
    const auto made   = core::hotSwitchDetail( core::Verb::Connect,    "DcP3");
    const auto broken = core::hotSwitchDetail( core::Verb::Disconnect, "DcP3");

    // The one phrase both carry, so a log consumer needs a single pattern.
    EXPECT_TRUE( made.starts_with(   "hot switching")) << made;
    EXPECT_TRUE( broken.starts_with( "hot switching")) << broken;

    // ...and the half that differs, which is what a reader actually needs.
    EXPECT_NE( made.find(   "closed"), std::string::npos) << made;
    EXPECT_NE( broken.find( "opened"), std::string::npos) << broken;
    EXPECT_NE( made, broken);

    EXPECT_NE( made.find( "DcP3"), std::string::npos) << made;
}

TEST( InterlockMessages, TheRefusalNamesEveryPartOfTheProblemAndTheWayOut)
{
    const auto message = core::liveTapMessage( "BatterySupply", "Dmm1", core::QuantityKind::Current, "DcP3");

    EXPECT_NE( message.find( "BatterySupply"), std::string::npos) << message;
    EXPECT_NE( message.find( "Dmm1"),          std::string::npos) << message;
    EXPECT_NE( message.find( "DcP3"),          std::string::npos) << message;
    EXPECT_NE( message.find( "Remove("),       std::string::npos) << message;
}

// ---------------------------------------------------------------------------
// The probe, through the two routing engines
// ---------------------------------------------------------------------------

TEST_F( InterlockTest, AColdConnectCarriesNoNote)
{
    mConnect( mock::Builder{ mSupply });

    EXPECT_EQ( onlyDetail(), "");
}

TEST_F( InterlockTest, ConnectingOntoALiveOutputCarriesTheNote)
{
    mSupply.Energised = true;

    mConnect( mock::Builder{ mSupply });

    EXPECT_NE( onlyDetail().find( "hot switching"), std::string::npos) << onlyDetail();
}

TEST_F( InterlockTest, DisconnectingALiveOutputCarriesTheNote)
{
    mSupply.Energised = true;

    mDisconnect( mock::Builder{ mSupply });

    EXPECT_NE( onlyDetail().find( "hot switching"), std::string::npos) << onlyDetail();
}

TEST_F( InterlockTest, ADetachedRunCannotHotSwitchBecauseNoContactMoved)
{
    mSupply.Energised = true;

    core::bench().detach();

    mDisconnect( mock::Builder{ mSupply });

    //
    // The detached marker, not the hot-switch note. A replayed run closed no
    // relay, so there was nothing to arc -- and this is the ordering
    // core::detail::postSourceEvent writes out rather than leaves implied.
    //
    EXPECT_EQ( onlyDetail(), core::kDetachedDetail);
}

TEST_F( InterlockTest, AConfigWithNothingToEnergiseIsNeverAskedAndNeverFlagged)
{
    //
    // The compile-time half of this test is that it compiles at all:
    // mock::PassiveConfig has no isEnergised, and core::detail::energisedNow's
    // static_assert does not fire for it because it has no applyDriver either.
    // A serial interface is not a thing that can be hot switched.
    //
    mConnect( mock::PassiveBuilder{});

    EXPECT_EQ( onlyDetail(), "");
}
