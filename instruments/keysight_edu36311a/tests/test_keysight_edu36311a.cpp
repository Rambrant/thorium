//
// hal::keysight_edu36311a::EDU36311A's own tests.
//
// Linked against this driver alone -- hal and core and nothing else -- which is
// what keeps this directory independently packageable (see
// instruments/README.md). A test here that wanted the fabric, a second
// instrument, or the N6701A whose isolation tags this driver re-declares would
// fail to link where it sits, and belongs in rig/tests/.
//
// Ids come from whatever the linking deployment declares rather than being
// spelled DcP5/DcP6/DcP7 -- see anyId() below. What is under test is a driver,
// and a driver does not know which rig it ended up in.
//
#include "hal/keysight_edu36311a.hpp"
#include "hal/verbs/route.hpp"
#include "hal/verbs/source.hpp"

#include "core/meta.hpp"
#include "core/verbs/interlock.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape rig/tests/test_safing.cpp uses
// for hal::SafeableInstrument, and framework/hal/tests/driver/test_address.cpp
// for the hal::ReachableOver mechanism itself).
//
// Gigabit LAN and a rear USBTMC port, and no GPIB connector at all -- the same
// back panel its Smart Bench Essentials sibling the EDU34450A has. So a rig row
// addressing one over GPIB does not compile, which is worth pinning down here
// because the E36300-series instruments this driver's SCPI comes from are not
// all like that.
//
// And no channel argument, which is where this differs from
// hal::keysight_n6701a::N6701A: an EDU36311A's three outputs are three types,
// not one type with a slot number (see the header on Output1).
//
namespace
{
    using hal::keysight_edu36311a::DirectOutput1;
    using hal::keysight_edu36311a::RelayOutput2;
    using hal::keysight_edu36311a::RelayOutput3;

    static_assert(   std::constructible_from< RelayOutput2, hal::InstrumentId, hal::Lan> );
    static_assert(   std::constructible_from< RelayOutput2, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< RelayOutput2, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< RelayOutput2, hal::InstrumentId, hal::Gpib> );
    static_assert( ! std::constructible_from< RelayOutput2, hal::InstrumentId, hal::Serial> );

    //
    // And an address is not optional: an instrument the PC cannot reach is not
    // an instrument a rig has -- see hal/topology/active_instruments.hpp on why
    // the INSTRUMENT() column is mandatory rather than defaulted.
    //
    static_assert( ! std::constructible_from< RelayOutput2, hal::InstrumentId> );

    //
    // A channel number handed in as well would be a rig writing down an output
    // that does not exist, which is the whole argument for the Output tags.
    //
    static_assert( ! std::constructible_from< RelayOutput2, hal::InstrumentId, hal::Lan, int> );

    //
    // The safing contract, asserted here rather than only in
    // rig/tests/test_safing.cpp: that file expands the rig's whole instrument
    // list and so can only run where a rig is linked, but whether *this* driver
    // satisfies the concept is a fact about this directory. On a source it is
    // also the most important of these assertions -- a supply that safeRig()
    // skipped would be a rail left live by a failed run.
    //
    static_assert( hal::SafeableInstrument< DirectOutput1> );
    static_assert( hal::SafeableInstrument< RelayOutput2> );
    static_assert( std::derived_from< RelayOutput2, hal::InstrumentTag> );

    //
    // hal::keysight_edu36311a::DirectWiring does not satisfy
    // SwitchableIsolation -- see that concept's own comment for why. Checked
    // the concept-wrapped way (see
    // core/tests/criteria/test_static_constraints.cpp on why a bare
    // static_assert( !requires{ ... }) is unreliable and this form is not):
    // this is the compile-time half of the guarantee, since a runtime test
    // cannot exercise "this does not compile" without breaking the build.
    //
    template<typename Output, typename Isolation>
    concept CanConnect = requires( hal::SwitchFabric & fabric,
                                   const hal::InstrumentWiring & iw,
                                   const hal::ConnectorWiring & cw,
                                   const hal::keysight_edu36311a::DcConfig<Output, Isolation> & config)
    {
        connectDriver(    fabric, iw, cw, config);
        disconnectDriver( fabric, iw, cw, config);
    };

    using hal::keysight_edu36311a::DirectWiring;
    using hal::keysight_edu36311a::Output1;
    using hal::keysight_edu36311a::Output2;
    using hal::keysight_edu36311a::RelayIsolated;

    static_assert(  CanConnect<Output2, RelayIsolated> );
    static_assert( !CanConnect<Output1, DirectWiring> );

    //
    // The bench fact behind those two, restated as an assertion because it is
    // the one thing about this rig's use of this driver that a reader is likely
    // to think arbitrary: output 1 is the 5 A output, every isolation relay on
    // this rack is rated 2 A, so output 1's lead cannot be switched and its row
    // says DirectOutput1. Nothing here enforces the arithmetic -- the relay's
    // rating is rig/devices.inc's business -- but the numbers it turns on are
    // this driver's, and they are checked below.
    //
    static_assert( Output1::MaxAmps  == 5.0);
    static_assert( Output1::MaxVolts == 6.0);
    static_assert( Output2::MaxAmps  == 1.0);
    static_assert( Output2::MaxVolts == 30.0);

    //
    // The actual point of SwitchableIsolation being a concept rather than a
    // per-tag connectDriver/disconnectDriver overload: a brand new
    // relay-having tag, never seen anywhere in hal/keysight_edu36311a.hpp,
    // still gets Connect/Disconnect for free. If those had instead been
    // written out per tag, this one would need its own copy before this
    // assertion could pass -- and, because this driver's .cpp holds free
    // functions rather than out-of-line members, it would also need an entry
    // in an explicit instantiation list that does not exist. See the header's
    // comment on namespace detail, which is where that consequence is argued.
    //
    struct HypotheticalContactorIsolation { static constexpr bool HasRelay = true; };

    static_assert( CanConnect<Output2, HypotheticalContactorIsolation> );

    //
    // Whatever the linking deployment's first instrument is called. A driver's
    // tests have no business knowing that this repo's bench rig calls these
    // three outputs DcP5, DcP6 and DcP7 -- see the top-level CMakeLists.txt on
    // the packageability defect that hard-coded ids in a driver's tests cause.
    //
    [[nodiscard]]
    auto anyId() -> hal::InstrumentId
    {
        return core::meta::values<hal::InstrumentId>[ 0];
    }
} // namespace

using namespace core::literals;
using namespace core::quantities;

using hal::keysight_edu36311a::RatingExceeded;

// ===========================================================================
// The three outputs
// ===========================================================================

TEST( Edu36311A, EachOutputKnowsItsChannelAndItsRating)
{
    EXPECT_EQ( DirectOutput1::channel(), 1);
    EXPECT_EQ( RelayOutput2::channel(),  2);
    EXPECT_EQ( RelayOutput3::channel(),  3);

    EXPECT_DOUBLE_EQ( DirectOutput1::maxVoltage().value(), 6.0);
    EXPECT_DOUBLE_EQ( DirectOutput1::maxCurrent().value(), 5.0);

    EXPECT_EQ( DirectOutput1::rating(), "6 V / 5 A");
    EXPECT_EQ( RelayOutput2::rating(),  "30 V / 1 A");
}

//
// Outputs 2 and 3 are the same rating and different channels, which is exactly
// where this model parts company from the E36311A whose programming guide
// covers its SCPI: there, channel 3 is a *negative* 25 V rail. Asserted so that
// a later edit made while reading that guide cannot quietly turn output 3 into
// the other instrument's.
//
TEST( Edu36311A, OutputsTwoAndThreeAreIdenticalExceptForTheirChannel)
{
    EXPECT_EQ( RelayOutput2::rating(), RelayOutput3::rating());

    EXPECT_DOUBLE_EQ( RelayOutput2::maxVoltage().value(), RelayOutput3::maxVoltage().value());
    EXPECT_DOUBLE_EQ( RelayOutput2::maxVoltage().value(), 30.0);

    EXPECT_NE( RelayOutput2::channel(), RelayOutput3::channel());
}

// ===========================================================================
// Sourcing and reading back, simulated
// ===========================================================================

TEST( Edu36311A, AppliedVoltageReadsBackAndRemovedOutputReadsZero)
{
    RelayOutput2 supply{ anyId(), hal::Simulated{} };

    supply.applyOutput( 24.0_V, 0.5_A, std::nullopt);

    EXPECT_TRUE( supply.isEnabled());
    EXPECT_DOUBLE_EQ( supply.measuredVoltage().rawMeasure().value(), 24.0);

    supply.removeOutput();

    EXPECT_FALSE( supply.isEnabled());

    //
    // Zero, not the setpoint -- what a real supply with its output off
    // actually reads, and the reading a script checking "is this rail really
    // off" depends on. The setpoint is still there for a report to render.
    //
    EXPECT_DOUBLE_EQ( supply.measuredVoltage().rawMeasure().value(), 0.0);
    EXPECT_DOUBLE_EQ( supply.outputVoltage().value(),                24.0);
}

TEST( Edu36311A, MeasuredCurrentComesFromTheSimulationHookAndNotFromTheLimit)
{
    RelayOutput2 supply{ anyId(), hal::Simulated{} };

    supply.setSimulatedOutputCurrent( 0.37_A);
    supply.applyOutput( 24.0_V, 0.5_A, std::nullopt);

    //
    // The limit is what the supply was told; the reading is what it is
    // delivering, and no setpoint determines it.
    //
    EXPECT_DOUBLE_EQ( supply.measuredCurrent().rawMeasure().value(), 0.37);
    EXPECT_DOUBLE_EQ( supply.currentLimit()->value(),                0.5);
}

TEST( Edu36311A, ApplyWithNothingSetIsZeroVoltsAndEnabled)
{
    RelayOutput2 supply{ anyId(), hal::Simulated{} };

    supply.applyOutput( std::nullopt, std::nullopt, std::nullopt);

    EXPECT_TRUE( supply.isEnabled());
    EXPECT_DOUBLE_EQ( supply.outputVoltage().value(), 0.0);
    EXPECT_FALSE( supply.currentLimit().has_value());
}

//
// Safing zeroes the setpoint as well as disabling the output, which Remove
// deliberately does not -- see EDU36311A::safe() for the unattended-re-enable
// argument. The current limit and the trip level survive both, also
// deliberately.
//
TEST( Edu36311A, SafeZeroesTheSetpointWhereRemoveLeavesIt)
{
    RelayOutput2 safed{   anyId(), hal::Simulated{} };
    RelayOutput2 removed{ anyId(), hal::Simulated{} };

    safed.applyOutput(   24.0_V, 0.5_A, 27.0_V);
    removed.applyOutput( 24.0_V, 0.5_A, 27.0_V);

    safed.safe();
    removed.removeOutput();

    EXPECT_FALSE( safed.isEnabled());
    EXPECT_FALSE( removed.isEnabled());

    EXPECT_DOUBLE_EQ( safed.outputVoltage().value(),   0.0);
    EXPECT_DOUBLE_EQ( removed.outputVoltage().value(), 24.0);

    EXPECT_DOUBLE_EQ( safed.currentLimit()->value(),           0.5);
    EXPECT_DOUBLE_EQ( safed.overVoltageProtection()->value(), 27.0);
}

TEST( Edu36311A, ApplyAndRemoveReachTheDriverThroughTheVerbs)
{
    RelayOutput2 supply{ anyId(), hal::Simulated{} };

    ApplyEngine  apply{};
    RemoveEngine remove{};

    apply( supply.dc().voltage( 12.0_V).currentLimit( 0.25_A));

    EXPECT_TRUE( supply.isEnabled());
    EXPECT_DOUBLE_EQ( supply.outputVoltage().value(), 12.0);

    remove( supply.dc());

    EXPECT_FALSE( supply.isEnabled());
}

//
// The interlock's question, which is the one thing Connect/Disconnect consult
// before moving a relay in this rail's lead.
//
TEST( Edu36311A, IsEnergisedFollowsTheOutputState)
{
    RelayOutput2 supply{ anyId(), hal::Simulated{} };

    EXPECT_FALSE( isEnergised( supply.dc().config()));

    supply.applyOutput( 24.0_V, std::nullopt, std::nullopt);

    EXPECT_TRUE( isEnergised( supply.dc().config()));
}

// ===========================================================================
// The rating check
// ===========================================================================
//
// The one place this driver overrules a script rather than relaying the
// instrument's judgement, and the reason is the asymmetry it removes: an
// attached supply answers -222 for 24 V on its 6 V output, and a simulated one
// answers nothing at all, so without this the same script passes in CI and
// fails on the bench. See RatingExceeded's own comment.
//
// Which makes these tests the load-bearing ones for that argument: every
// instrument in this repository's CI is simulated.
//

TEST( Edu36311ARating, VoltageBeyondAnOutputsBadgeIsRefusedSimulated)
{
    DirectOutput1 sixVolt{ anyId(), hal::Simulated{} };

    EXPECT_THROW( sixVolt.applyOutput( 24.0_V, std::nullopt, std::nullopt), RatingExceeded);

    // And the same voltage is fine on an output that has it.
    RelayOutput2 thirtyVolt{ anyId(), hal::Simulated{} };

    EXPECT_NO_THROW( thirtyVolt.applyOutput( 24.0_V, std::nullopt, std::nullopt));
}

TEST( Edu36311ARating, CurrentBeyondAnOutputsBadgeIsRefused)
{
    RelayOutput2 oneAmp{ anyId(), hal::Simulated{} };

    EXPECT_THROW( oneAmp.applyOutput( 5.0_V, 2.0_A, std::nullopt), RatingExceeded);

    // 5 A is this instrument's own maximum, and output 1 is where it lives.
    DirectOutput1 fiveAmp{ anyId(), hal::Simulated{} };

    EXPECT_NO_THROW( fiveAmp.applyOutput( 5.0_V, 5.0_A, std::nullopt));
}

//
// A trip level above what the output can produce is not a protection, it is a
// protection that can never fire -- and a script that wrote one believes the
// rail is guarded when it is not.
//
TEST( Edu36311ARating, AnOverVoltageTripLevelBeyondTheOutputsBadgeIsRefused)
{
    DirectOutput1 sixVolt{ anyId(), hal::Simulated{} };

    EXPECT_THROW( sixVolt.applyOutput( 5.0_V, std::nullopt, 12.0_V), RatingExceeded);
    EXPECT_NO_THROW( sixVolt.applyOutput( 5.0_V, std::nullopt, 5.5_V));
}

TEST( Edu36311ARating, TheRefusalNamesTheInstrumentTheSettingAndBothNumbers)
{
    DirectOutput1 sixVolt{ anyId(), hal::Simulated{} };

    try
    {
        sixVolt.applyOutput( 24.0_V, std::nullopt, std::nullopt);

        FAIL() << "expected a RatingExceeded";
    }
    catch( const RatingExceeded & refused)
    {
        const std::string message = refused.what();

        EXPECT_NE( message.find( core::meta::to_string( anyId())), std::string::npos) << message;
        EXPECT_NE( message.find( "voltage"),                       std::string::npos) << message;
        EXPECT_NE( message.find( "24"),                            std::string::npos) << message;
        EXPECT_NE( message.find( "6"),                             std::string::npos) << message;
    }
}

//
// Refused before anything is remembered, which matters for what a report says
// afterwards: a rail that was never programmed must not appear in the journal
// as one that was.
//
TEST( Edu36311ARating, ARefusedApplyLeavesTheOutputAsItWas)
{
    DirectOutput1 sixVolt{ anyId(), hal::Simulated{} };

    sixVolt.applyOutput( 5.0_V, std::nullopt, std::nullopt);

    EXPECT_THROW( sixVolt.applyOutput( 24.0_V, std::nullopt, std::nullopt), RatingExceeded);

    EXPECT_TRUE( sixVolt.isEnabled());
    EXPECT_DOUBLE_EQ( sixVolt.outputVoltage().value(), 5.0);
}

// ===========================================================================
// What a journal line says
// ===========================================================================

TEST( Edu36311A, DescribeConfigNamesWhichOfTheThreeOutputsWasProgrammed)
{
    RelayOutput2 supply{ anyId(), hal::Simulated{} };

    const auto described = describeConfig( supply.dc().voltage( 24.0_V).currentLimit( 0.5_A).overVoltageProtection( 27.0_V).config());

    EXPECT_NE( described.Settings.find( "output 2"),   std::string::npos) << described.Settings;
    EXPECT_NE( described.Settings.find( "30 V / 1 A"), std::string::npos) << described.Settings;
    EXPECT_NE( described.Settings.find( "24"),         std::string::npos) << described.Settings;
    EXPECT_NE( described.Settings.find( "ovp"),        std::string::npos) << described.Settings;
}

TEST( Edu36311A, DescribeConfigOmitsSettingsTheChainNeverNamed)
{
    RelayOutput2 supply{ anyId(), hal::Simulated{} };

    const auto described = describeConfig( supply.dc().voltage( 24.0_V).config());

    EXPECT_EQ( described.Settings.find( "ovp"),          std::string::npos) << described.Settings;
    EXPECT_EQ( described.Settings.find( "currentLimit"), std::string::npos) << described.Settings;
}

//
// ===========================================================================
// The wire: what this supply is actually told
// ===========================================================================
//
// Everything above tests the driver's simulated half, which is what every
// script test in this repository drives through. This half tests the other one:
// the exact SCPI this driver would put on a socket, asserted without a socket.
//
// Which is the whole reason hal::io::ITransport is an interface rather than a
// socket class (see hal/io/transport.hpp). A driver written against a concrete
// connection can only be checked against hardware, which means its command
// strings are verified by a human reading them once, at the moment they were
// typed -- and then never again, including after the edit that broke them.
//
// It matters more on a source than it did on the meter, and the reason is the
// ordering: a supply's Apply is four commands whose *sequence* is the safety
// argument (limit, then trip level, then setpoint, then output on -- see
// detail::program). A sequence is precisely the kind of thing that survives a
// careless edit while still compiling and still passing every test that only
// looks at the end state.
//
// What this cannot test, and no test here could: that the *instrument* accepts
// these strings. That is what the programming guide was read for (see
// src/keysight_edu36311a.cpp for the documents and the derivation) and what a
// bring-up run on the desk bench confirms. These tests prove the driver sends
// what its author intended, not that its author was right about the supply.
//
namespace
{
    //
    // A supply made of canned replies: it records every command it is given
    // and answers the five queries this driver asks, so a test can assert the
    // conversation and steer any part of it.
    //
    // Answering by rule rather than by a fixed script, deliberately. A test
    // that had to write out the whole exchange in order would have to state
    // the error-queue reads and the identity query that it does not care
    // about, and would then break whenever an unrelated part of the sequence
    // changed -- which is the failure mode that gets a test deleted rather
    // than fixed.
    //
    class FakeSupply final : public hal::io::ITransport
    {
        public:
            auto send( const std::string_view command) -> void override
            {
                mSent.emplace_back( command);

                if( command == "*IDN?")
                {
                    mReplies.emplace_back( Identity);
                }
                else if( command == "SYST:ERR?")
                {
                    //
                    // "+0,\"No error\"" is the empty queue, which is what an
                    // instrument that accepted everything answers -- see
                    // hal::io::ScpiSession::nextError().
                    //
                    if( Errors.empty())
                    {
                        mReplies.emplace_back( "+0,\"No error\"");
                    }
                    else
                    {
                        mReplies.emplace_back( Errors.front());
                        Errors.erase( Errors.begin());
                    }
                }
                else if( command == "*OPC?")
                {
                    mReplies.emplace_back( "1");
                }
                else if( command.starts_with( "MEAS:VOLT?"))
                {
                    mReplies.emplace_back( MeasuredVoltage);
                }
                else if( command.starts_with( "MEAS:CURR?"))
                {
                    mReplies.emplace_back( MeasuredCurrent);
                }
                else if( command.starts_with( "OUTP?"))
                {
                    mReplies.emplace_back( OutputState);
                }
                else if( !command.empty() && command.back() == '?')
                {
                    mReplies.emplace_back( "0");
                }
            }

            auto receive() -> std::string override
            {
                if( mReplies.empty())
                {
                    //
                    // Silence, which is exactly how a real instrument refuses a
                    // query -- so a driver bug that queries something this fake
                    // does not answer surfaces as the timeout it would surface
                    // as on the bench, rather than as an empty string that
                    // parses as zero.
                    //
                    throw hal::io::TransportTimeout( "nothing queued on the fake supply");
                }

                std::string reply = mReplies.front();

                mReplies.erase( mReplies.begin());

                return reply;
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "fake EDU36311A";
            }

            [[nodiscard]]
            auto sent() const -> const std::vector<std::string> &
            {
                return mSent;
            }

            std::string              Identity{ "Keysight Technologies,EDU36311A,CN61130007,1.0.2-1.0.1" };
            std::string              MeasuredVoltage{ "+2.40012000E+01" };
            std::string              MeasuredCurrent{ "+3.71000000E-01" };
            std::string              OutputState{ "1" };
            std::vector<std::string> Errors;

        private:
            std::vector<std::string> mSent;
            std::vector<std::string> mReplies;
    };

    //
    // A supply with a fake transport already installed, and the fake still
    // reachable. A struct rather than two locals because the driver takes
    // ownership of the transport (see EDU36311A::useTransport) and the test
    // still needs to read what was sent through it.
    //
    struct Bench
    {
        RelayOutput2  Supply{ anyId(), hal::Simulated{} };
        FakeSupply *  Wire{};
    };

    [[nodiscard]]
    auto attached() -> std::unique_ptr<Bench>
    {
        auto bench = std::make_unique<Bench>();
        auto fake  = std::make_unique<FakeSupply>();

        bench->Wire = fake.get();

        bench->Supply.useTransport( std::move( fake));

        return bench;
    }

    //
    // Everything the driver sent, with the session-opening exchange dropped --
    // the error-queue drain and the identity query, which happen once and which
    // most of these tests are not about. Asserted in full by the one test that
    // is about them.
    //
    [[nodiscard]]
    auto afterOpening( const FakeSupply & wire) -> std::vector<std::string>
    {
        auto commands = wire.sent();

        commands.erase( commands.begin(), commands.begin() + 2);

        return commands;
    }
} // namespace

//
// A transport wins over the address it was not opened from -- which is what
// makes every test below possible, and is stated as its own assertion so that
// the reason the rest of them work is written down once.
//
TEST( Edu36311AWire, AnInjectedTransportMakesTheDriverStopSimulating)
{
    RelayOutput2 supply{ anyId(), hal::Simulated{} };

    EXPECT_TRUE( supply.isSimulated());

    supply.useTransport( std::make_unique<FakeSupply>());

    EXPECT_FALSE( supply.isSimulated());
}

TEST( Edu36311AWire, OpeningASessionDrainsTheErrorQueueThenAsksWhatTheInstrumentIs)
{
    const auto bench = attached();

    static_cast<void>( bench->Supply.session());

    ASSERT_GE( bench->Wire->sent().size(), 2u);

    EXPECT_EQ( bench->Wire->sent()[ 0], "SYST:ERR?");
    EXPECT_EQ( bench->Wire->sent()[ 1], "*IDN?");
}

//
// The whole of an Apply, in order. This is the test the ordering argument in
// detail::program exists for: the limit is in force before the rail comes up,
// the trip level is above the setpoint before the setpoint exists, and the
// output is enabled last.
//
TEST( Edu36311AWire, ApplySendsLimitThenTripLevelThenSetpointThenOutputOn)
{
    const auto bench = attached();

    bench->Supply.applyOutput( 24.0_V, 0.5_A, 27.0_V);

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{
                   "CURR 0.5, (@2)",
                   "SYST:ERR?",
                   "VOLT:PROT 27, (@2)",
                   "SYST:ERR?",
                   "VOLT 24, (@2)",
                   "SYST:ERR?",
                   "OUTP 1, (@2)",
                   "SYST:ERR?",
                   "*OPC?" }));
}

//
// The channel list is this output's and no other's, which is the whole reason
// three drivers can share one chassis -- see the .cpp on why nothing here ever
// sends INSTrument:SELect.
//
TEST( Edu36311AWire, EveryCommandCarriesThisOutputsOwnChannel)
{
    RelayOutput3 third{ anyId(), hal::Simulated{} };

    auto  fake = std::make_unique<FakeSupply>();
    auto *wire = fake.get();

    third.useTransport( std::move( fake));

    third.applyOutput( 5.0_V, std::nullopt, std::nullopt);

    EXPECT_EQ( afterOpening( *wire),
               ( std::vector<std::string>{ "VOLT 5, (@3)", "SYST:ERR?", "OUTP 1, (@3)", "SYST:ERR?", "*OPC?" }));
}

TEST( Edu36311AWire, ApplyWithNothingSetStillProgramsADefinedRail)
{
    const auto bench = attached();

    bench->Supply.applyOutput( std::nullopt, std::nullopt, std::nullopt);

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{ "VOLT 0, (@2)", "SYST:ERR?", "OUTP 1, (@2)", "SYST:ERR?", "*OPC?" }));
}

//
// No *RST anywhere, ever -- it would zero all three outputs, and the other two
// belong to two other drivers behind this same address.
//
TEST( Edu36311AWire, NothingThisDriverSendsResetsTheWholeChassis)
{
    const auto bench = attached();

    bench->Supply.applyOutput( 24.0_V, 0.5_A, 27.0_V);
    bench->Supply.removeOutput();
    bench->Supply.safe();

    for( const auto & command : bench->Wire->sent())
    {
        EXPECT_EQ( command.find( "*RST"), std::string::npos) << command;
        EXPECT_EQ( command.find( "INST"), std::string::npos) << command;
    }
}

TEST( Edu36311AWire, RemoveTurnsTheOutputOffAndWaitsForIt)
{
    const auto bench = attached();

    bench->Supply.removeOutput();

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{ "OUTP 0, (@2)", "SYST:ERR?", "*OPC?" }));
}

TEST( Edu36311AWire, ReadbackIsOneQueryWithASpaceBeforeItsChannelList)
{
    const auto bench = attached();

    EXPECT_DOUBLE_EQ( bench->Supply.measuredVoltage().rawMeasure().value(), 24.0012);
    EXPECT_DOUBLE_EQ( bench->Supply.measuredCurrent().rawMeasure().value(),  0.371);

    //
    // The space is the instrument's own rule and not a formatting choice: a
    // channel list written straight against the "?" is answered with -103,
    // "invalid separator". A command, by contrast, takes a comma -- see the
    // Apply test above, and the .cpp's comment on the pair.
    //
    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{ "MEAS:VOLT? (@2)", "MEAS:CURR? (@2)" }));
}

//
// The interlock asks the instrument rather than this driver's memory, which is
// the point: an output turned on from the front panel is live, and a driver
// reading back its own last command would call it cold and close a relay into
// it.
//
TEST( Edu36311AWire, OutputIsOnAsksTheInstrument)
{
    const auto bench = attached();

    bench->Wire->OutputState = "1";

    EXPECT_TRUE( bench->Supply.outputIsOn());

    bench->Wire->OutputState = "0";

    EXPECT_FALSE( bench->Supply.outputIsOn());

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{ "OUTP? (@2)", "OUTP? (@2)" }));
}

//
// The whole reason those are two members: isEnabled() is an observer on the
// measurement path and reports what this driver commanded, outputIsOn() is
// what the Connect path asks and reports what the instrument says. Here they
// legitimately disagree -- nothing has been applied through this driver at all
// and the output is on -- and the one the interlock consults before moving a
// relay is the one that notices.
//
TEST( Edu36311AWire, IsEnergisedSaysLiveEvenWhenThisDriverNeverTurnedTheOutputOn)
{
    const auto bench = attached();

    bench->Wire->OutputState = "1";

    EXPECT_FALSE( bench->Supply.isEnabled());
    EXPECT_TRUE( isEnergised( bench->Supply.dc().config()));
}

//
// Safing on an attached supply: the output off and the setpoint zeroed, in that
// order, down the session that is already open.
//
TEST( Edu36311AWire, SafeTurnsTheOutputOffThenZeroesTheSetpoint)
{
    const auto bench = attached();

    bench->Supply.applyOutput( 24.0_V, std::nullopt, std::nullopt);

    const auto before = bench->Wire->sent().size();

    bench->Supply.safe();

    auto safing = bench->Wire->sent();

    safing.erase( safing.begin(), safing.begin() + static_cast<long>( before));

    //
    // write(), not checked() -- no SYST:ERR? in there, deliberately. Safing
    // runs on a path where the instrument may already be gone, and checked()
    // would both wait for a reply and throw an exception hal::safeRig() does
    // not catch. See detail::sendSafe.
    //
    EXPECT_EQ( safing, ( std::vector<std::string>{ "OUTP 0, (@2)", "VOLT 0, (@2)" }));
}

//
// The rule from instruments/README.md, and this instrument is the case it
// matters most for: safe() may use a session, it may not open one.
//
TEST( Edu36311AWire, SafeOnANeverUsedSupplyOpensNothing)
{
    RelayOutput2 supply{ anyId(), hal::Lan( "no-such-host.invalid") };

    //
    // A hostname nothing answers to. If safe() opened a session this would
    // throw out of a safing pass -- which is exactly the failure the rule
    // exists to prevent, since safeRig() does not catch and would then abandon
    // every instrument after this one.
    //
    EXPECT_NO_THROW( supply.safe());
}

TEST( Edu36311AWire, ASafingSendThatFailsIsSwallowed)
{
    const auto bench = attached();

    static_cast<void>( bench->Supply.session());

    //
    // A transport that has stopped answering is not what breaks sendSafe --
    // write() needs no reply. What is asserted here is the shape of the
    // contract: safing a supply whose session is open never throws, whatever
    // the wire does with the bytes.
    //
    EXPECT_NO_THROW( bench->Supply.safe());
}

// ===========================================================================
// The wrong instrument at the right address
// ===========================================================================

TEST( Edu36311AWire, AnInstrumentThatIsNotThisModelIsRefused)
{
    const auto bench = attached();

    bench->Wire->Identity = "Keysight Technologies,EDU34450A,MY60012345,01.00-01.00";

    EXPECT_THROW( static_cast<void>( bench->Supply.session()), hal::io::ScpiFault);
}

//
// The near miss, and the reason this driver's identity check is narrower than
// the meter's: an E36311A speaks exactly this command set, so every command
// this driver sends would be accepted -- and channel 2's limit is 25 V where
// this driver believes 30 V, while channel 3 is a *negative* rail. Both
// mistakes are silent, which is why the model has to be refused rather than
// tolerated.
//
TEST( Edu36311AWire, AnE36311AIsRefusedEvenThoughItSpeaksTheSameCommands)
{
    const auto bench = attached();

    bench->Wire->Identity = "Keysight Technologies,E36311A,MY59000123,1.0.2-1.0.1";

    try
    {
        static_cast<void>( bench->Supply.session());

        FAIL() << "expected an ScpiFault";
    }
    catch( const hal::io::ScpiFault & refused)
    {
        const std::string message = refused.what();

        EXPECT_NE( message.find( "E36311A"), std::string::npos) << message;
    }
}

TEST( Edu36311AWire, AnIdentityThatIsNotShapedLikeAnIdnReplyIsRefused)
{
    const auto bench = attached();

    bench->Wire->Identity = "hello";

    EXPECT_THROW( static_cast<void>( bench->Supply.session()), hal::io::ScpiFault);
}

//
// A supply that failed its identity check is asked again on the next command
// rather than being treated as verified -- the right way round for a bench,
// where the instrument that was off when the run started may be on now.
//
TEST( Edu36311AWire, AFailedIdentityCheckIsRetriedRatherThanRemembered)
{
    const auto bench = attached();

    bench->Wire->Identity = "Keysight Technologies,E36311A,MY59000123,1.0.2-1.0.1";

    EXPECT_THROW( static_cast<void>( bench->Supply.session()), hal::io::ScpiFault);

    bench->Wire->Identity = "Keysight Technologies,EDU36311A,CN61130007,1.0.2-1.0.1";

    EXPECT_NO_THROW( static_cast<void>( bench->Supply.session()));
}

// ===========================================================================
// A refused command
// ===========================================================================

//
// The failure hal::io::ScpiSession::checked() exists for, on the one kind of
// instrument where getting it wrong drives a DUT rather than merely misreports
// one: a refused setpoint followed by an accepted enable is a rail at the
// *previous* voltage with a script believing it set a new one.
//
TEST( Edu36311AWire, ASetpointTheInstrumentRefusesStopsTheApplyBeforeTheOutputComesOn)
{
    const auto bench = attached();

    // Open the session first, or the queued error is drained as the previous
    // user's -- see hal::io::ScpiSession::clearErrors and prepare().
    static_cast<void>( bench->Supply.session());

    bench->Wire->Errors = { "-222,\"Data out of range\"", "+0,\"No error\"" };

    EXPECT_THROW( bench->Supply.applyOutput( 30.0_V, std::nullopt, std::nullopt), hal::io::ScpiFault);

    for( const auto & command : afterOpening( *bench->Wire))
    {
        EXPECT_EQ( command.find( "OUTP 1"), std::string::npos)
            << "the output was enabled after a refused setpoint: " << command;
    }
}

TEST( Edu36311AWire, ARefusedCommandIsReportedWithTheCommandThatCausedIt)
{
    const auto bench = attached();

    static_cast<void>( bench->Supply.session());

    bench->Wire->Errors = { "-222,\"Data out of range\"", "+0,\"No error\"" };

    try
    {
        bench->Supply.applyOutput( 30.0_V, std::nullopt, std::nullopt);

        FAIL() << "expected an ScpiFault";
    }
    catch( const hal::io::ScpiFault & fault)
    {
        EXPECT_EQ( fault.command(), "VOLT 30, (@2)");
        EXPECT_EQ( fault.error().Code, -222);
    }
}

//
// And the rating check fires before any of that, which is the whole point of
// its being in the driver: nothing reaches the wire at all.
//
TEST( Edu36311AWire, ARatingRefusalSendsNothing)
{
    DirectOutput1 sixVolt{ anyId(), hal::Simulated{} };

    auto  fake = std::make_unique<FakeSupply>();
    auto *wire = fake.get();

    sixVolt.useTransport( std::move( fake));

    EXPECT_THROW( sixVolt.applyOutput( 24.0_V, std::nullopt, std::nullopt), RatingExceeded);

    EXPECT_TRUE( wire->sent().empty());
}
