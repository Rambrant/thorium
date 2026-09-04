//
// hal::keysight_edu34450a::EDU34450A's own tests.
//
// Linked against this driver alone -- hal and core and nothing else -- which is
// what keeps this directory independently packageable (see
// instruments/README.md). A test here that wanted the fabric, an Apply or a
// second instrument would fail to link where it sits, and belongs in
// rig/tests/.
//
// The suite is named for the driver rather than for the generic mechanism it
// exercises. instruments/keysight_edu34450a/tests is still under the historical
// HalInstrument name, deliberately, so that its move out of framework/hal stayed
// provably behaviour-preserving; this file has no such history to preserve.
//
#include "hal/keysight_edu34450a.hpp"

#include "core/meta.hpp"
#include "core/verbs/interlock.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <functional>
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
// Gigabit LAN and a rear USBTMC port, and no GPIB connector at all: the
// rack-mount 34450A takes a GPIB option, and this is the bench box that does
// not. So a rig row addressing one over GPIB does not compile.
//
namespace
{
    static_assert(   std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Lan> );
    static_assert(   std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Gpib> );
    static_assert( ! std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId, hal::Serial> );

    //
    // And an address is not optional: an instrument the PC cannot reach is not
    // an instrument a rig has -- see hal/topology/active_instruments.hpp on
    // why the INSTRUMENT() column is mandatory rather than defaulted.
    //
    static_assert( ! std::constructible_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentId> );

    //
    // The safing contract, asserted here rather than only in
    // rig/tests/test_safing.cpp: that file expands the rig's whole instrument
    // list and so can only run where a rig is linked, but whether *this* driver
    // satisfies the concept is a fact about this directory.
    //
    static_assert( hal::SafeableInstrument< hal::keysight_edu34450a::EDU34450A> );
    static_assert( std::derived_from< hal::keysight_edu34450a::EDU34450A, hal::InstrumentTag> );
} // namespace

using namespace core::literals;
using namespace core::quantities;

using hal::keysight_edu34450a::EDU34450A;

TEST( Edu34450A, ExposesBothVoltageAndCurrentPorts)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedVoltage( 5.02_V);
    dmm.setSimulatedCurrent( 0.5_A);

    EXPECT_DOUBLE_EQ( dmm.voltage().rawMeasure().value(), 5.02);
    EXPECT_DOUBLE_EQ( dmm.current().rawMeasure().value(), 0.5);
}

//
// Two ids taken from whatever the linking deployment declares, rather than the
// literal Dmm1/Dmm2 this would otherwise be written with -- see the same test
// in instruments/keysight_edu34450a/tests for the bench dependency that shape
// removes. What is under test is that the driver carries the id it was given
// through to its ports, which needs two distinct ids and does not care which
// two.
//
// Skipped rather than weakened on a deployment that declares only one: there is
// then nothing for an instrument to be distinguishable *from*, and a version of
// this that constructed two drivers on the same id would assert something true
// of a bug.
//
TEST( Edu34450A, TwoMetersAreDistinguishableByInstrumentId)
{
    constexpr auto ids = core::meta::values<hal::InstrumentId>;

    if constexpr( ids.size() < 2)
    {
        GTEST_SKIP() << "this deployment declares one instrument -- no second id to differ from";
    }
    else
    {
        EDU34450A first{  ids[ 0], hal::Simulated{} };
        EDU34450A second{ ids[ 1], hal::Simulated{} };

        EXPECT_EQ( first.voltage().instrumentId(),  ids[ 0]);
        EXPECT_EQ( second.voltage().instrumentId(), ids[ 1]);
    }
}

TEST( Edu34450A, AcVoltagePortReadsTheAcSimulatedReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedVoltage( 5.0_V);
    dmm.setSimulatedAcVoltage( 230.0_V);

    EXPECT_DOUBLE_EQ( dmm.acVoltage().rawMeasure().value(), 230.0);
}

TEST( Edu34450A, AcCurrentPortReadsTheAcSimulatedReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedCurrent( 0.5_A);
    dmm.setSimulatedAcCurrent( 1.2_A);

    EXPECT_DOUBLE_EQ( dmm.acCurrent().rawMeasure().value(), 1.2);
}

TEST( Edu34450A, VoltageAfterAcVoltageSwitchesBackToDcMode)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedVoltage( 5.0_V);
    dmm.setSimulatedAcVoltage( 230.0_V);

    (void)dmm.acVoltage();

    EXPECT_DOUBLE_EQ( dmm.voltage().rawMeasure().value(), 5.0);
    EXPECT_EQ( dmm.mode(), EDU34450A::Mode::Dc);
}

TEST( Edu34450A, ModeIsSharedAcrossPortHandlesHeldPastAModeSwitch)
{
    // Documents the known, accepted sharp edge from EDU34450A's own comment: a
    // port handle obtained before a mode switch still reads whichever mode is
    // current when rawMeasure() is eventually called, not the mode active when
    // the handle was created. It mirrors the instrument, which has one
    // measurement front end and one SCPI FUNCtion setting.
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedVoltage( 5.0_V);
    dmm.setSimulatedAcVoltage( 230.0_V);

    auto dcPort = dmm.voltage();
    (void)dmm.acVoltage();

    EXPECT_DOUBLE_EQ( dcPort.rawMeasure().value(), 230.0);
}

TEST( Edu34450A, ResistancePortReadsTheTwoWireSimulatedReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedResistance( 100.0_Ohm);
    dmm.setSimulatedFourWireResistance( 99.5_Ohm);

    EXPECT_DOUBLE_EQ( dmm.resistance().rawMeasure().value(), 100.0);
}

TEST( Edu34450A, FourWireResistancePortReadsTheFourWireSimulatedReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedResistance( 100.0_Ohm);
    dmm.setSimulatedFourWireResistance( 99.5_Ohm);

    EXPECT_DOUBLE_EQ( dmm.fourWireResistance().rawMeasure().value(), 99.5);
}

TEST( Edu34450A, ResistanceAfterFourWireResistanceSwitchesBackToTwoWireMode)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedResistance( 100.0_Ohm);
    dmm.setSimulatedFourWireResistance( 99.5_Ohm);

    (void)dmm.fourWireResistance();

    EXPECT_DOUBLE_EQ( dmm.resistance().rawMeasure().value(), 100.0);
    EXPECT_EQ( dmm.resistanceMode(), EDU34450A::ResistanceMode::TwoWire);
}

//
// The sense requirement is in the port's *type*, not in its runtime setup, so
// this is a static_assert rather than an EXPECT -- a two-wire reading and a
// four-wire one are different types, and core::MeasureEngine branches on that
// with if constexpr.
//
TEST( Edu34450A, FourWireResistanceRequiresTheSensePathButTwoWireDoesNot)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };

    static_assert( decltype( dmm.resistance())::SenseUse         == core::SensePath::NotUsed);
    static_assert( decltype( dmm.fourWireResistance())::SenseUse == core::SensePath::Required);

    // The chained setup builders preserve it -- .range() on a four-wire port
    // must not quietly hand back a two-wire one.
    static_assert( decltype( dmm.fourWireResistance().range( 1000.0_Ohm))::SenseUse == core::SensePath::Required);

    (void) dmm;
}

//
// The FREQ function is its own function on this meter, not the AC half of the
// voltage one -- so what the DC/AC keys were last set to says nothing about
// what a frequency reading returns.
//
TEST( Edu34450A, FrequencyPortReadsRegardlessOfWhichModeTheMeterIsIn)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedFrequency( 50.0_Hz);

    EXPECT_DOUBLE_EQ( dmm.frequency().rawMeasure().value(), 50.0);

    (void)dmm.acVoltage();

    EXPECT_DOUBLE_EQ( dmm.frequency().rawMeasure().value(), 50.0);
}

//
// A frequency *reading* and the frequency *setting* on an AC port are two
// different things a dot apart -- see EDU34450A::frequency()'s own comment.
// This asserts they stay that way: setting the hint on an AC voltage port
// leaves that port a voltage port, and does not touch the counter.
//
TEST( Edu34450A, TheFrequencySettingOnAnAcPortIsNotAFrequencyReading)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedAcVoltage( 230.0_V);
    dmm.setSimulatedFrequency( 50.0_Hz);

    const auto port = dmm.acVoltage().frequency( 50.0_Hz);

    static_assert( std::same_as< decltype( port.rawMeasure()), Voltage> );

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 230.0);
    EXPECT_EQ( port.setup().Frequency, std::optional{ 50.0_Hz});
}

//
// The one function on this meter that neither the L4411A nor anything else on
// this bench has, and the reason core grew a farad. Its own function like FREQ,
// so likewise unaffected by which of the DC/AC keys was last pressed.
//
TEST( Edu34450A, CapacitancePortReadsRegardlessOfWhichModeTheMeterIsIn)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    dmm.setSimulatedCapacitance( 470.0_uF);

    EXPECT_DOUBLE_EQ( dmm.capacitance().rawMeasure().value(), 470.0e-6);

    (void)dmm.acVoltage();

    EXPECT_DOUBLE_EQ( dmm.capacitance().rawMeasure().value(), 470.0e-6);
}

//
// Two claims about the capacitance port's type, and the second is the one that
// matters on a rig.
//
// There is no 4-wire capacitance: the instrument has one capacitance function
// and it is 2-wire, so the port must not carry a sense requirement.
//
// And a capacitance reading is one core::requiresDeadNode names -- the meter
// charges the node from its own current source (see core/verbs/interlock.hpp)
// -- so core::MeasureEngine refuses to route one to a pin an energised supply
// is cabled onto. That check is an `if constexpr` on the port's quantity, so it
// is settled here, by the type this returns, and asserted for real against this
// rig's SOURCE_WIRING in rig/tests/test_interlock.cpp.
//
TEST( Edu34450A, CapacitanceIsATwoWireReadingThatRequiresADeadNode)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };

    static_assert( decltype( dmm.capacitance())::SenseUse == core::SensePath::NotUsed);

    static_assert(   core::requiresDeadNode( core::quantityKindOf<Capacitance>()));
    static_assert( ! core::requiresDeadNode( core::quantityKindOf<Voltage>()));

    (void) dmm;
}

//
// Slow is what the instrument resets to and what its accuracy specifications
// are quoted at, so it is what this driver starts in -- see Resolution's own
// comment on why this axis is instrument state and not a MeasureSetup field.
//
TEST( Edu34450A, ResolutionDefaultsToSlowAndIsInstrumentStateNotPortState)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };

    EXPECT_EQ( dmm.resolution(), EDU34450A::Resolution::Slow);

    dmm.setResolution( EDU34450A::Resolution::Fast);

    EXPECT_EQ( dmm.resolution(), EDU34450A::Resolution::Fast);

    // Survives a function change, exactly as the front-panel setting does.
    (void)dmm.acCurrent();

    EXPECT_EQ( dmm.resolution(), EDU34450A::Resolution::Fast);
}

//
// safe() is a no-op on a meter, and this is what "no-op" has to mean: the
// instrument's last function and resolution are still readable afterwards.
// Nothing about them can energise the DUT, and safing runs after a script has
// already died, so there is no reason to discard the one piece of state still
// worth reading. rig/tests/test_safing.cpp asserts the same thing through
// hal::safeRig() over the whole rig; this asserts it about the driver.
//
TEST( Edu34450A, SafeLeavesTheMetersOwnStateAlone)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
    (void)dmm.acVoltage();
    dmm.setResolution( EDU34450A::Resolution::Medium);

    dmm.safe();

    EXPECT_EQ( dmm.mode(),       EDU34450A::Mode::Ac);
    EXPECT_EQ( dmm.resolution(), EDU34450A::Resolution::Medium);
}

//
// The address survives construction and reads back as what the rig declared --
// and is now what the session is opened from, rather than a value nothing read.
// See the wire tests below, and isSimulated() for the one branch it decides.
//
TEST( Edu34450A, CarriesTheAddressItWasDeclaredWith)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Lan( "bench-dmm1") };

    EXPECT_EQ( to_string( dmm.address()), "Lan bench-dmm1:5025");
}

//
// ===========================================================================
// The wire: what this meter is actually told
// ===========================================================================
//
// Everything above tests the driver's simulated half, which is what every
// script test in this repository reads through. This half tests the other one:
// the exact SCPI this driver would put on a socket, asserted without a socket.
//
// Which is the whole reason hal::io::ITransport is an interface rather than a
// socket class (see hal/io/transport.hpp). A driver written against a concrete
// connection can only be checked against hardware, which means its command
// strings are verified by a human reading them once, at the moment they were
// typed -- and then never again, including after the edit that broke them. Here
// they are verified by assertion, in CI, on a machine with no instruments.
//
// What this cannot test, and no test here could: that the *instrument* accepts
// these strings. That is what the programmer's reference was read for (see
// src/keysight_edu34450a.cpp for the document and the page-by-page derivation)
// and what a bring-up run on the desk bench confirms (see dev/README.md). The
// division is worth being explicit about -- these tests prove the driver sends
// what its author intended, not that its author was right about the meter.
//
namespace
{
    //
    // A meter made of canned replies: it records every command it is given and
    // answers the four queries this driver asks, so a test can assert the
    // conversation and steer any part of it.
    //
    // Answering by rule rather than by a fixed script, deliberately. A test
    // that had to write out the whole exchange in order would have to state the
    // error-queue reads and the identity query that it does not care about, and
    // would then break whenever an unrelated part of the sequence changed --
    // which is the failure mode that gets a test deleted rather than fixed.
    // Here a test states only the part it is about (the reading, the queued
    // error, the identity) and asserts only the commands it is about.
    //
    class FakeMeter final : public hal::io::ITransport
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
                else if( command == "READ?")
                {
                    mReplies.emplace_back( Reading);
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
                    throw hal::io::TransportTimeout( "nothing queued on the fake meter");
                }

                std::string reply = mReplies.front();

                mReplies.erase( mReplies.begin());

                return reply;
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "fake EDU34450A";
            }

            [[nodiscard]]
            auto sent() const -> const std::vector<std::string> &
            {
                return mSent;
            }

            //
            // What this fake claims to be, what it answers READ? with, and what
            // it has queued in its error queue. Public and settable, because
            // steering them is the whole point.
            //
            std::string              Identity{ "Keysight Technologies,EDU34450A,MY60012345,01.00-01.00" };
            std::string              Reading{  "+5.02010000E+00" };
            std::vector<std::string> Errors;

        private:
            std::vector<std::string> mSent;
            std::vector<std::string> mReplies;
    };

    //
    // A meter with a fake transport already installed, and the fake still
    // reachable. Returned as a pair because the driver takes ownership of the
    // transport (see EDU34450A::useTransport) and the test still needs to read
    // what was sent through it.
    //
    struct Bench
    {
        EDU34450A   Dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };
        FakeMeter * Meter{};
    };

    [[nodiscard]]
    auto attachedMeter() -> Bench
    {
        Bench bench;

        auto meter = std::make_unique<FakeMeter>();

        bench.Meter = meter.get();

        bench.Dmm.useTransport( std::move( meter));

        return bench;
    }

    //
    // Everything the driver sent, with the session-opening exchange dropped --
    // the error-queue drain and the identity query, which happen once and which
    // most of these tests are not about. Asserted in full by the one test that
    // is about them.
    //
    [[nodiscard]]
    auto commandsAfterOpening( const FakeMeter & meter) -> std::vector<std::string>
    {
        auto commands = meter.sent();

        commands.erase( commands.begin(), commands.begin() + 2);

        return commands;
    }
} // namespace

//
// A transport wins over the address it was not opened from -- which is what
// makes every test below possible, and is stated as its own assertion so that
// the reason the rest of them work is written down once.
//
TEST( Edu34450AWire, AnInjectedTransportMakesTheDriverStopSimulating)
{
    EDU34450A dmm{ hal::InstrumentId::Dmm1, hal::Simulated{} };

    EXPECT_TRUE( dmm.isSimulated());

    dmm.useTransport( std::make_unique<FakeMeter>());

    EXPECT_FALSE( dmm.isSimulated());
}

//
// The simulation hooks are not consulted at all once there is an instrument,
// and this is the test that proves the branch rather than assuming it: the
// simulated voltage and the meter's reply are deliberately different numbers.
//
TEST( Edu34450AWire, ReadsTheInstrumentRatherThanTheSimulationHooks)
{
    auto bench = attachedMeter();

    bench.Dmm.setSimulatedVoltage( 12.0_V);
    bench.Meter->Reading = "+5.02010000E+00";

    EXPECT_DOUBLE_EQ( bench.Dmm.voltage().rawMeasure().value(), 5.0201);
}

//
// The whole exchange for one reading, in order, including the two commands the
// other tests drop. Five commands for the first reading: drain whatever the
// last user left queued, ask what this box is, configure the function, check
// that it took the configuration, read.
//
TEST( Edu34450AWire, OpensASessionByDrainingTheErrorQueueAndCheckingTheIdentity)
{
    auto bench = attachedMeter();

    static_cast<void>( bench.Dmm.voltage().rawMeasure());

    EXPECT_EQ( bench.Meter->sent(), ( std::vector<std::string>{
        "SYST:ERR?",
        "*IDN?",
        "CONF:VOLT:DC",
        "SYST:ERR?",
        "READ?" }));
}

//
// And only once: the second reading configures and reads, and does not ask the
// instrument what it is again.
//
TEST( Edu34450AWire, OpensTheSessionOnceAndReconfiguresPerReading)
{
    auto bench = attachedMeter();

    static_cast<void>( bench.Dmm.voltage().rawMeasure());
    static_cast<void>( bench.Dmm.voltage().rawMeasure());

    EXPECT_EQ( commandsAfterOpening( *bench.Meter), ( std::vector<std::string>{
        "CONF:VOLT:DC",
        "SYST:ERR?",
        "READ?",
        "CONF:VOLT:DC",
        "SYST:ERR?",
        "READ?" }));
}

//
// A range from the port becomes the CONFigure command's <range> argument, and
// the resolution rides along with it -- which is the instrument's own rule
// rather than a preference: <resolution> may only accompany an explicit
// <range>, since the meter cannot fix an integration time for a range it has
// not chosen yet.
//
TEST( Edu34450AWire, ARangeAndTheResolutionGoInOneConfigureCommand)
{
    auto bench = attachedMeter();

    bench.Dmm.setResolution( EDU34450A::Resolution::Fast);

    static_cast<void>( bench.Dmm.voltage().range( 10.0_V).rawMeasure());

    EXPECT_EQ( commandsAfterOpening( *bench.Meter).front(), "CONF:VOLT:DC 10,3.0E-5");
}

//
// Autoranging is the absence of a range rather than a value, so the command
// carries no argument at all -- and nothing sets the resolution, because
// CONFigure has just reset it to this meter's default, which is the 5.5 digits
// Resolution::Slow names.
//
TEST( Edu34450AWire, AutorangingAtTheDefaultResolutionSendsNeitherArgument)
{
    auto bench = attachedMeter();

    static_cast<void>( bench.Dmm.voltage().rawMeasure());

    EXPECT_EQ( commandsAfterOpening( *bench.Meter), ( std::vector<std::string>{
        "CONF:VOLT:DC",
        "SYST:ERR?",
        "READ?" }));
}

//
// Autoranging at a resolution that is *not* the default cannot send both in one
// command, so the resolution follows as its own SENSe command. The one case
// where this driver sends three commands to take one reading, and the reason
// the command table carries a SENSe path per function at all.
//
TEST( Edu34450AWire, AutorangingAtANonDefaultResolutionSetsItSeparately)
{
    auto bench = attachedMeter();

    bench.Dmm.setResolution( EDU34450A::Resolution::Medium);

    static_cast<void>( bench.Dmm.voltage().rawMeasure());

    EXPECT_EQ( commandsAfterOpening( *bench.Meter), ( std::vector<std::string>{
        "CONF:VOLT:DC",
        "SYST:ERR?",
        "VOLT:DC:RES 2.0E-5",
        "SYST:ERR?",
        "READ?" }));
}

//
// One port accessor per row of the meter's function set, and this is the table
// that says which SCPI function each one selects -- including the two pairs
// where the C++ type is the same and the instrument function is not (DC/AC
// volts and amps, 2-wire/4-wire ohms), which is the reason the driver has a
// Function enum rather than dispatching on the quantity alone.
//
TEST( Edu34450AWire, EachPortSelectsItsOwnInstrumentFunction)
{
    struct Expectation
    {
        std::string_view                       Command;
        std::function<void( EDU34450A &)>      Read;
    };

    const Expectation expectations[] = {
        { "CONF:VOLT:DC", []( EDU34450A & dmm) { static_cast<void>( dmm.voltage().rawMeasure());            } },
        { "CONF:VOLT:AC", []( EDU34450A & dmm) { static_cast<void>( dmm.acVoltage().rawMeasure());          } },
        { "CONF:CURR:DC", []( EDU34450A & dmm) { static_cast<void>( dmm.current().rawMeasure());            } },
        { "CONF:CURR:AC", []( EDU34450A & dmm) { static_cast<void>( dmm.acCurrent().rawMeasure());          } },
        { "CONF:RES",     []( EDU34450A & dmm) { static_cast<void>( dmm.resistance().rawMeasure());         } },
        { "CONF:FRES",    []( EDU34450A & dmm) { static_cast<void>( dmm.fourWireResistance().rawMeasure()); } },
        { "CONF:FREQ",    []( EDU34450A & dmm) { static_cast<void>( dmm.frequency().rawMeasure());          } },
        { "CONF:CAP",     []( EDU34450A & dmm) { static_cast<void>( dmm.capacitance().rawMeasure());        } },
    };

    for( const auto & [ command, read] : expectations)
    {
        auto bench = attachedMeter();

        read( bench.Dmm);

        EXPECT_EQ( commandsAfterOpening( *bench.Meter).front(), command) << command;
    }
}

//
// The two functions with no discrete resolution get a range and nothing else,
// and neither is an omission. Capacitance is fixed at 3.5 digits on this meter
// -- there is no resolution parameter, and sending one is an error rather than
// a no-op. Frequency's resolution is in hertz, so Slow/Medium/Fast has nothing
// to say about it; note that its <range> argument is the approximate frequency
// of the input signal rather than a range, which is what a
// MeasureSetup<Frequency>::Range happens to be exactly the right shape for.
//
TEST( Edu34450AWire, CapacitanceAndFrequencyTakeNoResolutionEvenWithARange)
{
    {
        auto bench = attachedMeter();

        bench.Dmm.setResolution( EDU34450A::Resolution::Fast);
        bench.Meter->Reading = "+9.80000000E-04";

        static_cast<void>( bench.Dmm.capacitance().range( Capacitance{ 0.001}).rawMeasure());

        EXPECT_EQ( commandsAfterOpening( *bench.Meter).front(), "CONF:CAP 0.001");
    }
    {
        auto bench = attachedMeter();

        bench.Dmm.setResolution( EDU34450A::Resolution::Fast);
        bench.Meter->Reading = "+5.00000000E+01";

        static_cast<void>( bench.Dmm.frequency().range( 50.0_Hz).rawMeasure());

        EXPECT_EQ( commandsAfterOpening( *bench.Meter).front(), "CONF:FREQ 50");
    }
}

//
// The reading is parsed out of the instrument's own number format, which is
// always exponential and always signed -- "+5.02010000E+00", never "5.0201".
//
TEST( Edu34450AWire, ParsesTheInstrumentsNumberFormatIncludingItsLeadingSign)
{
    auto bench = attachedMeter();

    bench.Meter->Reading = "-1.23456000E-03";

    EXPECT_DOUBLE_EQ( bench.Dmm.voltage().rawMeasure().value(), -0.00123456);
}

//
// Two readings come back when the meter's secondary display is on, which is a
// front-panel state a previous user can leave behind and which this model's
// SCPI offers no documented way to turn off. The primary reading is the first;
// the second is discarded, since this driver exposes no port for it.
//
TEST( Edu34450AWire, TakesThePrimaryReadingWhenTheSecondaryDisplayIsAlsoAnswering)
{
    auto bench = attachedMeter();

    bench.Meter->Reading = "+5.02010000E+00,+1.20000000E-02";

    EXPECT_DOUBLE_EQ( bench.Dmm.voltage().rawMeasure().value(), 5.0201);
}

//
// An overload -- the input beyond the range the meter was told to use -- is the
// instrument's third answer, and becomes the one this framework already has a
// name for. core::MeasureEngine catches it, records NaN with the reason beside
// it, and the run carries on to the next check; see core/driver/port.hpp.
//
// The reason names the range, because the range is the fix.
//
TEST( Edu34450AWire, AnOverloadBecomesAnUnmeasurableReadingNamingTheRange)
{
    auto bench = attachedMeter();

    bench.Meter->Reading = "+9.90000000E+37";

    try
    {
        static_cast<void>( bench.Dmm.voltage().range( 10.0_V).rawMeasure());

        FAIL() << "an overload reading should not come back as a number";
    }
    catch( const core::UnmeasurableReading & unmeasurable)
    {
        EXPECT_EQ( unmeasurable.reason(), "DC voltage overload -- the input is beyond the 10 range");
    }
}

TEST( Edu34450AWire, AnOverloadWhileAutorangingSaysSoRatherThanNamingARange)
{
    auto bench = attachedMeter();

    bench.Meter->Reading = "-9.90000000E+37";

    try
    {
        static_cast<void>( bench.Dmm.resistance().rawMeasure());

        FAIL() << "an overload reading should not come back as a number";
    }
    catch( const core::UnmeasurableReading & unmeasurable)
    {
        EXPECT_EQ( unmeasurable.reason(), "2-wire resistance overload -- the input is beyond autoranging");
    }
}

//
// A configuration the instrument refused is a fault and not a reading, and the
// exception names the command that caused it. This is the failure the error-queue
// check exists for: without it the READ? below would return a perfectly
// plausible number measured on whatever the meter was set to before, and
// nothing anywhere would record that the range asked for was never applied.
//
// The error is queued after a first, successful reading rather than before it,
// and that ordering is itself the point: an error queued before the session
// opens is drained by the session-opening exchange and belongs to whoever had
// the meter last (see EDU34450A::session). This one belongs to this command.
TEST( Edu34450AWire, ARefusedConfigurationThrowsNamingTheCommandAndTheInstrumentsWords)
{
    auto bench = attachedMeter();

    static_cast<void>( bench.Dmm.voltage().rawMeasure());

    bench.Meter->Errors = { "-222,\"Data out of range\"" };

    try
    {
        static_cast<void>( bench.Dmm.voltage().range( 5000.0_V).rawMeasure());

        FAIL() << "a refused configuration should not produce a reading";
    }
    catch( const hal::io::ScpiFault & fault)
    {
        EXPECT_EQ( fault.command(), "CONF:VOLT:DC 5000,1.5E-6");
        EXPECT_EQ( fault.error(),   ( hal::io::ScpiError{ -222, "Data out of range" }));
    }

    // And no reading was attempted after the refusal.
    EXPECT_EQ( commandsAfterOpening( *bench.Meter).back(), "SYST:ERR?");
}

//
// The wrong instrument at the right address -- a re-cabled rack, a DHCP lease
// that moved, a copied row in the instrument table -- is refused before any
// reading is taken. Without this check the run would be full of readings from
// a different box, and some of them would pass.
//
TEST( Edu34450AWire, RefusesAnInstrumentThatIsNotThisModel)
{
    auto bench = attachedMeter();

    bench.Meter->Identity = "Keysight Technologies,DSO8064A,MY40001234,06.16";

    EXPECT_THROW( static_cast<void>( bench.Dmm.voltage().rawMeasure()), hal::io::ScpiFault);

    // Nothing was configured and nothing was read.
    EXPECT_EQ( bench.Meter->sent(), ( std::vector<std::string>{ "SYST:ERR?", "*IDN?" }));
}

//
// And the rack-mount sibling is accepted, because this driver is honestly for
// both: the 34450A and the EDU34450A share one command set -- which is why the
// 34450A's programmer's reference is the document this driver was written
// against -- and differ in packaging and connectors.
//
TEST( Edu34450AWire, AcceptsTheRackMount34450AWhoseCommandSetThisIs)
{
    auto bench = attachedMeter();

    bench.Meter->Identity = "Agilent Technologies,34450A,MY55001234,01.00-01.00";

    EXPECT_NO_THROW( static_cast<void>( bench.Dmm.voltage().rawMeasure()));
}

//
// identity() is the same question, asked on purpose rather than as a side
// effect of the first reading -- what a run's traceability header should carry
// about an instrument is what the instrument says it is.
//
TEST( Edu34450AWire, IdentityReportsWhatTheInstrumentSaysItIs)
{
    auto bench = attachedMeter();

    EXPECT_EQ( bench.Dmm.identity(), "Keysight Technologies,EDU34450A,MY60012345,01.00-01.00");
}

//
// Dropping the session closes the connection; the next reading opens a new one,
// which is visible as the session-opening exchange happening a second time.
//
TEST( Edu34450AWire, ClosingTheSessionSendsTheDriverBackToSimulating)
{
    auto bench = attachedMeter();

    static_cast<void>( bench.Dmm.voltage().rawMeasure());

    bench.Dmm.closeSession();

    //
    // Back to simulating, because this driver was constructed on a Simulated
    // address: the injected transport is gone and there is nothing to reopen.
    // On a real Lan( ...) row the next reading would open a fresh socket
    // instead -- which is the same rule read the other way, and is what makes
    // closeSession() a recovery step rather than a shutdown.
    //
    EXPECT_TRUE( bench.Dmm.isSimulated());

    bench.Dmm.setSimulatedVoltage( 3.3_V);

    EXPECT_DOUBLE_EQ( bench.Dmm.voltage().rawMeasure().value(), 3.3);
}
