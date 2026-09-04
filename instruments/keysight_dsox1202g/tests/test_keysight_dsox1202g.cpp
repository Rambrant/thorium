//
// hal::keysight_dsox1202g::DSOX1202G's own tests.
//
// Written against the scope on the bench rather than ported wholesale from
// instruments/keysight_dsox1202g/tests, and the differences between the two
// files are the point: where a test here reads differently from its Infiniium
// counterpart, it is because the instrument does something different, and the
// test says which. Two channels rather than four, one reason for an
// unmeasurable reading rather than nineteen, an acquisition type where the
// other has a mode and a flag, and a back panel with one connector on it.
//
// Suite name Dsox1202G, following the newest driver package in the tree
// (Edu34450A) rather than the historic HalInstrument suite the older drivers
// still use because their tests were lifted out of framework/hal wholesale.
//
#include "hal/keysight_dsox1202g.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <vector>

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape rig/tests/test_safing.cpp uses
// for hal::SafeableInstrument, and hal/tests/driver/test_address.cpp for the
// hal::ReachableOver mechanism itself).
//
// One connector, and this is the assertion that carries the most weight in this
// file. The 1000 X-Series has a USB device port and nothing else: the
// programmer's guide says "There is no LAN interface (only USB is supported)",
// and it is the four-channel DSOX1204A/G in the next series that gained one. So
// Lan( "bench-osc1") -- which is exactly what this rig's instrument.inc said
// while an Infiniium was on this row -- does not compile against this driver,
// which is the whole job hal::ReachableOver exists to do.
//
namespace
{
    static_assert(   std::constructible_from< hal::keysight_dsox1202g::DSOX1202G, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< hal::keysight_dsox1202g::DSOX1202G, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::keysight_dsox1202g::DSOX1202G, hal::InstrumentId, hal::Lan> );
    static_assert( ! std::constructible_from< hal::keysight_dsox1202g::DSOX1202G, hal::InstrumentId, hal::Gpib> );
    static_assert( ! std::constructible_from< hal::keysight_dsox1202g::DSOX1202G, hal::InstrumentId, hal::Serial> );
    static_assert( ! std::constructible_from< hal::keysight_dsox1202g::DSOX1202G, hal::InstrumentId> );
} // namespace

using namespace core::literals;
using namespace core::quantities;

namespace
{
    //
    // ValidChannel's compile-time bound, checked the concept-wrapped way -- see
    // core/tests/criteria/test_static_constraints.cpp's own comment for why a
    // bare static_assert(!requires{...}) is unreliable and this
    // routing-through-a-concept form isn't.
    //
    // channel<3>() is the one that matters: it is valid on the scope this
    // driver replaces and invalid here, so a script that was not re-read when
    // the bench changed fails to build rather than quietly measuring channel 3
    // of a two-channel instrument.
    //
    template<unsigned N>
    concept CanChannel = requires( hal::keysight_dsox1202g::DSOX1202G & osc) { osc.template channel<N>(); };

    static_assert(  CanChannel<1> );
    static_assert(  CanChannel<2> );
    static_assert( !CanChannel<0> );
    static_assert( !CanChannel<3> );
} // namespace

TEST( Dsox1202G, VppPortReturnsSimulatedReading)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);

    auto port = osc1.channel<1>().vpp();

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 3.3);
    EXPECT_EQ( port.instrumentId(), hal::InstrumentId::Osc1);
}

TEST( Dsox1202G, PortOutlivesTheTemporaryChannelViewThatCreatedIt)
{
    //
    // The dangling-reference trap the Infiniium driver's own history records:
    // channel<1>() returns a temporary Channel<1>, gone by the next statement
    // -- Port must bind to the real, long-lived instrument, not to that
    // temporary, or this reads garbage (or crashes) instead of 3.3.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);

    auto port = osc1.channel<1>().vpp();  // the channel view temporary is gone after this line

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 3.3);  // still valid here
}

TEST( Dsox1202G, ExposesTheWholeAmplitudeFamily)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVmax( 1, 1.8_V);
    osc1.setSimulatedVmin( 1, -0.4_V);
    osc1.setSimulatedVrms( 1, 1.2_V);
    osc1.setSimulatedVaverage( 1, 0.9_V);

    auto ch1 = osc1.channel<1>();

    EXPECT_DOUBLE_EQ( ch1.vpp().rawMeasure().value(),      3.3);
    EXPECT_DOUBLE_EQ( ch1.vmax().rawMeasure().value(),     1.8);
    EXPECT_DOUBLE_EQ( ch1.vmin().rawMeasure().value(),    -0.4);
    EXPECT_DOUBLE_EQ( ch1.vrms().rawMeasure().value(),     1.2);
    EXPECT_DOUBLE_EQ( ch1.vaverage().rawMeasure().value(), 0.9);
}

namespace
{
    //
    // vmiddle() is not part of that family here, and its absence is a fact
    // about the instrument rather than an omission: there is no
    // :MEASure:VMIDdle on the 1000 X-Series. Asserted rather than left to a
    // comment, because the tempting fix -- computing it from vtop and vbase --
    // would put a number in the run journal that no instrument answered.
    //
    template<typename ChannelT>
    concept HasVmiddle = requires( ChannelT & channel) { channel.vmiddle(); };

    static_assert( !HasVmiddle< hal::keysight_dsox1202g::Channel<1>> );
} // namespace

TEST( Dsox1202G, ChannelsAreIndependentlyAddressedSimulatedData)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVpp( 2, 5.0_V);

    EXPECT_DOUBLE_EQ( osc1.channel<1>().vpp().rawMeasure().value(), 3.3);
    EXPECT_DOUBLE_EQ( osc1.channel<2>().vpp().rawMeasure().value(), 5.0);
}

TEST( Dsox1202G, ExposesTheTimingFamily)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedFrequency( 2, 1_kHz);
    osc1.setSimulatedPeriod( 2, 1_ms);
    osc1.setSimulatedRiseTime( 2, Time{ 12e-9});
    osc1.setSimulatedFallTime( 2, Time{ 14e-9});

    auto ch2 = osc1.channel<2>();

    EXPECT_DOUBLE_EQ( ch2.frequency().rawMeasure().value(), 1000.0);
    EXPECT_DOUBLE_EQ( ch2.period().rawMeasure().value(),    1e-3);
    EXPECT_DOUBLE_EQ( ch2.riseTime().rawMeasure().value(),  12e-9);
    EXPECT_DOUBLE_EQ( ch2.fallTime().rawMeasure().value(),  14e-9);
}

TEST( Dsox1202G, ExposesPulseWidths)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedPositiveWidth( 2, 20_us);
    osc1.setSimulatedNegativeWidth( 2, 30_us);

    auto ch2 = osc1.channel<2>();

    EXPECT_DOUBLE_EQ( ch2.positiveWidth().rawMeasure().value(), 20e-6);
    EXPECT_DOUBLE_EQ( ch2.negativeWidth().rawMeasure().value(), 30e-6);
}

TEST( Dsox1202G, RiseTimeDefaultsToTenNinetyThresholds)
{
    //
    // A threshold is part of what a rise time means, so a bare .riseTime() has
    // to be a complete reading rather than an underspecified one -- and 10/90
    // is also what this instrument measures by default (:MEASure:DEFine
    // THResholds STANdard).
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto port = osc1.channel<1>().riseTime();

    ASSERT_TRUE( port.setup().LowThreshold.has_value());
    ASSERT_TRUE( port.setup().HighThreshold.has_value());
    EXPECT_DOUBLE_EQ( *port.setup().LowThreshold,  0.1);
    EXPECT_DOUBLE_EQ( *port.setup().HighThreshold, 0.9);
}

TEST( Dsox1202G, RiseTimeThresholdsAreOverridable)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto port = osc1.channel<1>().riseTime().lowThreshold( 0.2).highThreshold( 0.8);

    EXPECT_DOUBLE_EQ( *port.setup().LowThreshold,  0.2);
    EXPECT_DOUBLE_EQ( *port.setup().HighThreshold, 0.8);
}

TEST( Dsox1202G, DefaultsToVppMode)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_EQ( osc1.mode(), hal::keysight_dsox1202g::DSOX1202G::Mode::Vpp);
    EXPECT_EQ( osc1.channelNumber(), 1u);
}

TEST( Dsox1202G, ChannelIsSharedAcrossPortHandlesHeldPastAChannelSwitch)
{
    //
    // The documented sharp edge, asserted so it stays documented: a Port bound
    // to the instrument reads whichever channel is current when rawMeasure()
    // runs, not the one that was current when the handle was taken. Harmless
    // for Measure( port, at( ...))'s read-immediately-and-discard usage, and
    // the price of Port never referencing a temporary channel view.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVpp( 2, 5.0_V);

    auto held = osc1.channel<1>().vpp();

    (void) osc1.channel<2>().vpp();

    EXPECT_DOUBLE_EQ( held.rawMeasure().value(), 5.0);
}

//
// ---------------------------------------------------------------------
// The Setup surface
// ---------------------------------------------------------------------
//

TEST( Dsox1202G, TriggerSetupRecordsEveryFieldItWasGiven)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.trigger()
                     .edgeSource<2>()
                     .slope( hal::keysight_dsox1202g::TriggerSlope::Falling)
                     .level( 4.8_V)
                     .sweep( hal::keysight_dsox1202g::TriggerSweep::Auto)
                     .coupling( hal::keysight_dsox1202g::TriggerCoupling::Dc)
                     .reject( hal::keysight_dsox1202g::TriggerReject::HighFrequency)
                     .holdoff( 1_ms)
                     .config());

    EXPECT_EQ( osc1.triggerSource(),   2u);
    EXPECT_EQ( osc1.triggerSlope(),    hal::keysight_dsox1202g::TriggerSlope::Falling);
    EXPECT_EQ( osc1.triggerSweep(),    hal::keysight_dsox1202g::TriggerSweep::Auto);
    EXPECT_EQ( osc1.triggerCoupling(), hal::keysight_dsox1202g::TriggerCoupling::Dc);
    EXPECT_EQ( osc1.triggerReject(),   hal::keysight_dsox1202g::TriggerReject::HighFrequency);
    ASSERT_TRUE( osc1.triggerLevel().has_value());
    EXPECT_DOUBLE_EQ( osc1.triggerLevel()->value(), 4.8);
    ASSERT_TRUE( osc1.triggerHoldoff().has_value());
    EXPECT_DOUBLE_EQ( osc1.triggerHoldoff()->value(), 1e-3);
}

TEST( Dsox1202G, EitherEdgeIsATriggerSlopeThisInstrumentActuallyHas)
{
    //
    // The one place this scope can do something its predecessor could not, and
    // it is worth a test of its own because the other driver's comment says so
    // explicitly: ":TRIGGER:EDGE:SLOPE EITHER" does not exist on an Infiniium,
    // "either-edge triggering is an InfiniiVision feature". This is an
    // InfiniiVision.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.trigger().slope( hal::keysight_dsox1202g::TriggerSlope::Either).config());

    EXPECT_EQ( osc1.triggerSlope(), hal::keysight_dsox1202g::TriggerSlope::Either);
}

TEST( Dsox1202G, ASetupLeavesFieldsItDidNotNameAlone)
{
    //
    // An unset field means "leave whatever is already configured", so a Setup
    // naming only the level must not reset the slope to a default this file
    // chose. Two Setups, and the first one's slope has to survive the second.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.trigger().slope( hal::keysight_dsox1202g::TriggerSlope::Rising).config());
    setupDriver( osc1.trigger().level( 2.5_V).config());

    EXPECT_EQ( osc1.triggerSlope(), hal::keysight_dsox1202g::TriggerSlope::Rising);
    ASSERT_TRUE( osc1.triggerLevel().has_value());
    EXPECT_DOUBLE_EQ( osc1.triggerLevel()->value(), 2.5);
}

TEST( Dsox1202G, TimebaseAndAcquisitionAreSeparateSubsystems)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.timebase()
                     .timePerDivision( 10_ms)
                     .position( 0_s)
                     .reference( hal::keysight_dsox1202g::TimebaseReference::Left)
                     .config());

    setupDriver( osc1.acquisition()
                     .type( hal::keysight_dsox1202g::AcquisitionType::HighResolution)
                     .config());

    ASSERT_TRUE( osc1.timePerDivision().has_value());
    EXPECT_DOUBLE_EQ( osc1.timePerDivision()->value(), 0.01);
    EXPECT_EQ( osc1.timebaseReference(), hal::keysight_dsox1202g::TimebaseReference::Left);
    EXPECT_EQ( osc1.acquisitionType(),   hal::keysight_dsox1202g::AcquisitionType::HighResolution);
}

TEST( Dsox1202G, AveragingSelectsTheTypeAndCarriesItsCount)
{
    //
    // One call setting two things, because on this instrument they are one
    // decision: :ACQuire:TYPE AVERage and :ACQuire:COUNt. A count set without
    // the type would sit unused until some later script selected averaging and
    // inherited it -- exactly the kind of carried-over state a reproducible
    // test must not depend on.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.acquisition().averagedOver( 16).config());

    EXPECT_EQ( osc1.acquisitionType(), hal::keysight_dsox1202g::AcquisitionType::Averaged);
    EXPECT_EQ( osc1.averageCount(),    16u);
}

namespace
{
    //
    // Memory depth and sample rate are query-only on this instrument
    // (:ACQuire:POINts? and :ACQuire:SRATe? have no command form), so the
    // Infiniium driver's points()/automaticPoints()/sampleRate() have nothing
    // to send and are absent rather than accepted-and-ignored. Asserted,
    // because "accepted and ignored" is what a driver written by copying the
    // other one would have produced, and nothing else here would notice.
    //
    template<typename BuilderT>
    concept CanSetPoints = requires( const BuilderT & builder) { builder.points( 1000u); };

    template<typename BuilderT>
    concept CanSetSampleRate = requires( const BuilderT & builder) { builder.sampleRate( core::quantities::Frequency{ 1e9 }); };

    static_assert( !CanSetPoints< hal::keysight_dsox1202g::AcquisitionBuilder> );
    static_assert( !CanSetSampleRate< hal::keysight_dsox1202g::AcquisitionBuilder> );
} // namespace

TEST( Dsox1202G, ChannelSetupAppliesToTheNamedChannelOnly)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.channel<2>()
                     .coupling( hal::keysight_dsox1202g::Coupling::Dc)
                     .voltsPerDivision( 100_mV)
                     .verticalOffset( 5_V)
                     .bandwidth( hal::keysight_dsox1202g::Bandwidth::Limited)
                     .probeAttenuation( 10.0)
                     .display( hal::keysight_dsox1202g::ChannelDisplay::On)
                     .config());

    EXPECT_EQ( osc1.channelCoupling( 2),  hal::keysight_dsox1202g::Coupling::Dc);
    EXPECT_EQ( osc1.channelBandwidth( 2), hal::keysight_dsox1202g::Bandwidth::Limited);
    EXPECT_EQ( osc1.probeAttenuation( 2), 10.0);
    EXPECT_EQ( osc1.channelDisplay( 2),   hal::keysight_dsox1202g::ChannelDisplay::On);
    ASSERT_TRUE( osc1.voltsPerDivision( 2).has_value());
    EXPECT_DOUBLE_EQ( osc1.voltsPerDivision( 2)->value(), 0.1);
    ASSERT_TRUE( osc1.verticalOffset( 2).has_value());
    EXPECT_DOUBLE_EQ( osc1.verticalOffset( 2)->value(), 5.0);

    // ...and channel 1 was not touched by any of it.
    EXPECT_FALSE( osc1.channelCoupling( 1).has_value());
    EXPECT_FALSE( osc1.voltsPerDivision( 1).has_value());
}

namespace
{
    //
    // A channel view is a temporary, and the builder it returns outlives it --
    // the same trap core::Port had. A builder holding Channel<N>& rather than
    // the instrument and the number would dangle here.
    //
    static_assert( std::constructible_from< hal::keysight_dsox1202g::ChannelBuilder,
                                            hal::keysight_dsox1202g::DSOX1202G &, unsigned> );

    //
    // The trigger source is bounded the same way channel<N>() is.
    //
    template<unsigned N>
    concept CanTriggerOn = requires( hal::keysight_dsox1202g::TriggerBuilder & trigger) { trigger.template edgeSource<N>(); };

    static_assert(  CanTriggerOn<1> );
    static_assert(  CanTriggerOn<2> );
    static_assert( !CanTriggerOn<0> );
    static_assert( !CanTriggerOn<3> );

    //
    // A scope has no output to energise, so Apply/Remove on one is "no matching
    // function" rather than a call that quietly does nothing. Worth asserting
    // on this model in particular: it has a built-in waveform generator, which
    // is an output -- it is simply not modelled here, and this is what says so
    // in a way that cannot rot.
    //
    template<typename ConfigT>
    concept HasApplyDriver = requires( const ConfigT & config) { applyDriver( config); };

    static_assert( !HasApplyDriver< hal::keysight_dsox1202g::TriggerConfig> );
    static_assert( !HasApplyDriver< hal::keysight_dsox1202g::ChannelConfig> );

    //
    // ...and Arm/Await are answered for by the capture config and by nothing
    // else on this instrument: Arm( Osc1.trigger()) is a compile error.
    //
    template<typename ConfigT>
    concept HasArmDriver = requires( const ConfigT & config) { armDriver( config); };

    static_assert(  HasArmDriver< hal::keysight_dsox1202g::SingleConfig> );
    static_assert( !HasArmDriver< hal::keysight_dsox1202g::TriggerConfig> );

    //
    // `Setup( Osc1.channel<2>())` -- a Setup naming a channel and no setting,
    // which can only be a mistake -- has to be a compile error rather than a
    // call that does nothing. That is exactly the absence of config() on the
    // channel view, asserted in both directions: the builder every setting
    // method returns does have one.
    //
    template<typename T>
    concept HasConfig = requires( T & subject) { subject.config(); };

    static_assert(  HasConfig< hal::keysight_dsox1202g::ChannelBuilder> );
    static_assert( !HasConfig< hal::keysight_dsox1202g::Channel<2>> );
} // namespace

TEST( Dsox1202G, ChannelBuilderOutlivesTheTemporaryChannelViewThatCreatedIt)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto builder = osc1.channel<2>().voltsPerDivision( 50_mV);   // view is gone after this line

    setupDriver( builder.config());

    ASSERT_TRUE( osc1.voltsPerDivision( 2).has_value());
    EXPECT_DOUBLE_EQ( osc1.voltsPerDivision( 2)->value(), 0.05);
}

//
// ---------------------------------------------------------------------
// Arm and Await
// ---------------------------------------------------------------------
//

TEST( Dsox1202G, ArmingLeavesTheScopeArmedAndTheCaptureCompletes)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_FALSE( osc1.isArmed());

    armDriver( osc1.single().timeout( 2_s).config());

    EXPECT_TRUE( osc1.isArmed());
    EXPECT_TRUE( awaitDriver( osc1.single().config()));
    EXPECT_FALSE( osc1.isArmed());
    EXPECT_TRUE( osc1.lastAcquisitionCompleted());
}

TEST( Dsox1202G, ACaptureThatNeverTriggersReportsNotCompleted)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedCaptureCompletes( false);

    armDriver( osc1.single().config());

    EXPECT_FALSE( awaitDriver( osc1.single().config()));
    EXPECT_FALSE( osc1.lastAcquisitionCompleted());
}

TEST( Dsox1202G, AwaitingWithoutArmingReportsNotCompleted)
{
    //
    // A script that measures a transient without arming a capture has measured
    // whatever was left in the acquisition buffer. That is a wrong answer, and
    // this is the check that catches it -- so it answers false rather than
    // throwing and abandoning the rest of the run.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_FALSE( awaitDriver( osc1.single().config()));
}

TEST( Dsox1202G, ArmingClearsAnyPreviousCompletion)
{
    //
    // A second capture must not be answered by the first one's completion. On
    // this instrument that is the RUN bit in :OPERegister:CONDition?, which is
    // set again by :SINGle -- so an Await that ran before the new arm took
    // effect would read the old stopped state as this capture's.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    armDriver( osc1.single().config());
    EXPECT_TRUE( awaitDriver( osc1.single().config()));

    armDriver( osc1.single().config());

    EXPECT_FALSE( osc1.lastAcquisitionCompleted());
}

TEST( Dsox1202G, SafingDisarmsAPendingCaptureAndLeavesSettingsAlone)
{
    //
    // A scope left armed after a script died is waiting for an event that is no
    // longer coming, and the next script's Await would be answered by it.
    // Settings are deliberately NOT reset -- see DSOX1202G::safe().
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.timebase().timePerDivision( 10_ms).config());
    armDriver( osc1.single().config());

    osc1.safe();

    EXPECT_FALSE( osc1.isArmed());
    ASSERT_TRUE( osc1.timePerDivision().has_value());
    EXPECT_DOUBLE_EQ( osc1.timePerDivision()->value(), 0.01);
}

//
// ---------------------------------------------------------------------
// Measurements the instrument cannot make
// ---------------------------------------------------------------------
//

TEST( Dsox1202G, AnUnmeasurableReadingThrowsWithTheOnlyReasonThisScopeGives)
{
    //
    // One reason, and this test is where that limitation is nailed down. The
    // guide gives a single sentence and a single sentinel: a measurement that
    // cannot be made returns +9.9E+37, "typically because the proper portion of
    // the waveform is not displayed". There is no :MEASure:SENDvalid on this
    // family and so no result-state code to turn into words.
    //
    // The Infiniium driver's equivalent test asserts "required edge not found",
    // which is a thing that scope can actually say. This one cannot, and a
    // driver that guessed between "no edge" and "clipped" would be putting
    // words in the instrument's mouth.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedUnmeasurable( 2, hal::keysight_dsox1202g::DSOX1202G::Mode::RiseTime);

    auto port = osc1.channel<2>().riseTime();

    try
    {
        (void)port.rawMeasure();
        FAIL() << "expected core::UnmeasurableReading";
    }
    catch( const core::UnmeasurableReading & unmeasurable)
    {
        EXPECT_EQ( unmeasurable.reason(), hal::keysight_dsox1202g::kUnmeasurable);
    }
}

TEST( Dsox1202G, AnUnmeasurableReadingIsPerMeasurementNotPerChannel)
{
    //
    // A clipped trace still has a perfectly good period, and a flat trace with
    // no edge on it has a vmax and no rise time. A single "this channel is
    // broken" flag could express neither -- and that is still true on an
    // instrument that will not say which of the two it is looking at.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVmax( 2, 4.8_V);
    osc1.setSimulatedRiseTime( 2, Time{ 12e-9});
    osc1.setSimulatedUnmeasurable( 2, hal::keysight_dsox1202g::DSOX1202G::Mode::RiseTime);

    EXPECT_DOUBLE_EQ( osc1.channel<2>().vmax().rawMeasure().value(), 4.8);
    EXPECT_THROW( (void)osc1.channel<2>().riseTime().rawMeasure(), core::UnmeasurableReading);
}

TEST( Dsox1202G, AnUnmeasurableReadingCanBeCleared)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVmin( 1, -0.4_V);
    osc1.setSimulatedUnmeasurable( 1, hal::keysight_dsox1202g::DSOX1202G::Mode::Vmin);

    EXPECT_THROW( (void)osc1.channel<1>().vmin().rawMeasure(), core::UnmeasurableReading);

    osc1.clearSimulatedUnmeasurable( 1, hal::keysight_dsox1202g::DSOX1202G::Mode::Vmin);

    EXPECT_DOUBLE_EQ( osc1.channel<1>().vmin().rawMeasure().value(), -0.4);
}

TEST( Dsox1202G, ExposesTheBaselineFamilyTheTransientCalculationNeeds)
{
    //
    // vbase() is not vmin(): base is the settled level the waveform spends its
    // time at, min is the most extreme sample in the record. Their difference is
    // the size of the negative transient -- the calculation
    // suite/scripts/ac_dropout_script.cpp performs, and the one the legacy ATE
    // assembled by hand out of a screen median and a manually subtracted
    // vertical offset.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVbase( 2, 5.0_V);
    osc1.setSimulatedVtop( 2, 5.1_V);
    osc1.setSimulatedVmin( 2, 4.62_V);
    osc1.setSimulatedVamplitude( 2, 0.1_V);

    auto ch2 = osc1.channel<2>();

    EXPECT_DOUBLE_EQ( ch2.vbase().rawMeasure().value(),      5.0);
    EXPECT_DOUBLE_EQ( ch2.vtop().rawMeasure().value(),       5.1);
    EXPECT_DOUBLE_EQ( ch2.vamplitude().rawMeasure().value(), 0.1);

    const auto transient = ch2.vbase().rawMeasure() - ch2.vmin().rawMeasure();

    EXPECT_NEAR( transient.value(), 0.38, 1e-9);
}

//
// ---------------------------------------------------------------------
// What the run journal is told
// ---------------------------------------------------------------------
//

TEST( Dsox1202G, DescribesOnlyTheSettingsASetupNamed)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto described = describeConfig(
        osc1.trigger().edgeSource<2>().slope( hal::keysight_dsox1202g::TriggerSlope::Falling).config());

    EXPECT_EQ( described.Instrument, "Osc1");
    EXPECT_EQ( described.Settings,   "trigger.source=2, trigger.slope=Falling");
}

TEST( Dsox1202G, ChannelSettingsAreDescribedAgainstTheirOwnChannel)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto described = describeConfig(
        osc1.channel<2>().coupling( hal::keysight_dsox1202g::Coupling::Dc).voltsPerDivision( 100_mV).config());

    EXPECT_EQ( described.Settings, "ch2.coupling=Dc, ch2.perDiv=100 mV");
}

TEST( Dsox1202G, AProbeRatioIsDescribedTheWayAProbeIsLabelled)
{
    //
    // "10x", not "10.000000" -- see describeAttenuation on why this driver
    // renders its own fragment rather than reaching for one of hal's.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto tenToOne     = describeConfig( osc1.channel<1>().probeAttenuation( 10.0).config());
    const auto twentyToOne   = describeConfig( osc1.channel<1>().probeAttenuation( 20.0).config());
    const auto direct       = describeConfig( osc1.channel<1>().probeAttenuation( 1.0).config());
    const auto divider      = describeConfig( osc1.channel<1>().probeAttenuation( 0.1).config());

    EXPECT_EQ( tenToOne.Settings,   "ch1.probe=10x");
    EXPECT_EQ( direct.Settings,     "ch1.probe=1x");
    EXPECT_EQ( divider.Settings,    "ch1.probe=0.1x");

    //
    // 20 is here because 10 alone did not catch the bug this test was written
    // with: trimming trailing zeros without checking for a decimal point turns
    // both into "1x" under a standard library whose std::to_string already
    // returns the shortest form. Any ratio ending in a zero would do; two of
    // them say it is the trailing zero and not the number.
    //
    EXPECT_EQ( twentyToOne.Settings, "ch1.probe=20x");
}

TEST( Dsox1202G, AnAveragingCountIsDescribedOnlyWhenAveragingIsWhatWasSelected)
{
    //
    // A count beside "type=HighResolution" would be a number the instrument is
    // not using, which is worse than no number: a reader diagnosing a noisy
    // capture would spend time on it.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto averaged  = describeConfig( osc1.acquisition().averagedOver( 16).config());
    const auto highRes   = describeConfig(
        osc1.acquisition().type( hal::keysight_dsox1202g::AcquisitionType::HighResolution).config());

    EXPECT_EQ( averaged.Settings, "acquire.type=Averaged, acquire.averages=16");
    EXPECT_EQ( highRes.Settings,  "acquire.type=HighResolution");
}

TEST( Dsox1202G, ACaptureAlwaysDescribesTheTimeoutsItWillUse)
{
    //
    // The one place an unset field is filled in for the log, and deliberately
    // so: a timeout is the number that decides how a failing capture behaves,
    // so a report of a run that timed out has to say what it was waiting for.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto defaulted = describeConfig( osc1.single().config());
    const auto named     = describeConfig( osc1.single().timeout( 2_s).config());

    EXPECT_EQ( defaulted.Settings, "single.timeout=5 s, single.armTimeout=1 s");
    EXPECT_EQ( named.Settings,     "single.timeout=2 s, single.armTimeout=1 s");
}

//
// Waveform transfer -- the :WAVeform half, for core::FetchEngine. See
// core/verbs/trace.hpp for the verb and core::Waveform for what comes back.
//

TEST( Dsox1202G, HandsBackTheCapturedRecordOffTheNamedChannel)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto trace = core::Waveform{
        core::quantityKindOf<core::quantities::Voltage>(),
        core::Waveform::Timing{ -1_ms, core::quantities::Time{ 1e-06 } },
        std::vector<double>{ 5.0, 4.6, 4.8, 5.0 } };

    osc1.setSimulatedTrace( 2, trace);

    EXPECT_EQ( fetchDriver( osc1.channel<2>().waveform().config()), trace);
}

TEST( Dsox1202G, AnsweringAChannelNothingCapturedGivesAnEmptyTrace)
{
    //
    // Empty rather than an exception, for the reason awaitAcquisition answers
    // false rather than throwing: a script reading out a record it never
    // captured has not crashed, and the check it feeds is where that surfaces.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_TRUE( fetchDriver( osc1.channel<2>().waveform().config()).empty());
}

TEST( Dsox1202G, FilesEachChannelsTraceUnderItsOwnSessionKey)
{
    //
    // Two channels hold two records at once, so one slot for both would let an
    // injected channel-1 trace answer a channel-2 Fetch. Two is fewer than the
    // Infiniium's four and changes nothing about the argument: one slot for two
    // records is already one too few.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_EQ( traceQualifier( osc1.channel<1>().waveform().config()), "Channel1");
    EXPECT_EQ( traceQualifier( osc1.channel<2>().waveform().config()), "Channel2");
}

TEST( Dsox1202G, WaveformBuilderCarriesItsChannelByValue)
{
    //
    // Unlike the fourteen measurement methods, this does NOT switch the
    // instrument's selected channel -- so the sharp edge they carry (a handle
    // taken before a later switch reads the later channel) does not exist here.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    osc1.setSimulatedTrace( 1, core::Waveform{
        core::quantityKindOf<core::quantities::Voltage>(),
        core::Waveform::Timing{},
        std::vector<double>{ 3.3 } });

    const auto channelOne = osc1.channel<1>().waveform();

    (void) osc1.channel<2>().vpp();   // would switch the instrument for a Measure

    ASSERT_EQ( fetchDriver( channelOne.config()).size(), 1u);
    EXPECT_DOUBLE_EQ( fetchDriver( channelOne.config()).at( 0), 3.3);
}

TEST( Dsox1202G, ATraceIsDescribedByWhichChannelItCameOff)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto described = describeConfig( osc1.channel<2>().waveform().config());

    EXPECT_EQ( described.Instrument, "Osc1");
    EXPECT_EQ( described.Settings,   "ch2");
}

// ===========================================================================
//
// Everything above tests this driver's simulated half, which is what every
// script test in this repository reads through. This half tests the other one:
// the exact SCPI this driver would put on the wire, asserted without an
// instrument.
//
// Which is the whole reason hal::io::ITransport is an interface rather than a
// concrete connection (see hal/io/transport.hpp). A driver written against a
// real session can only be checked against hardware, which means its command
// strings are verified by a human reading them once, at the moment they were
// typed -- and never again, including after the edit that broke them. Here they
// are verified by assertion, in CI, on a machine with no instruments and no
// VISA.
//
// What this cannot test, and no test here could: that the *scope* accepts these
// strings. That is what the programmer's guide was read for (see
// src/keysight_dsox1202g.cpp for the document and the derivation) and what a
// bring-up run on the desk confirms. These tests prove the driver sends what
// its author intended, not that its author was right about the instrument.
//
namespace
{
    //
    // A scope made of canned replies: it records every command it is given and
    // answers the queries this driver asks, so a test can assert the
    // conversation and steer any part of it.
    //
    // Answering by rule rather than by a fixed script, for the reason the
    // EDU34450A's own fake gives: a test that had to write out the whole
    // exchange in order would have to state the error-queue reads and the
    // identity query it does not care about, and would break whenever an
    // unrelated part of the sequence changed.
    //
    // The two registers are the interesting part. `Armed` and `Running` are
    // what :AER? and :OPERegister:CONDition? answer, and a test steers a capture
    // by setting them -- which is as close as anything can get to a scope that
    // did or did not trigger.
    //
    class FakeScope final : public hal::io::ITransport
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
                else if( command == ":AER?")
                {
                    mReplies.emplace_back( Armed ? "1" : "0");
                }
                else if( command == ":OPERegister:CONDition?")
                {
                    mReplies.emplace_back( Running ? "8" : "0");
                }
                else if( command == ":WAVeform:PREamble?")
                {
                    mReplies.emplace_back( Preamble);
                }
                else if( command == ":WAVeform:DATA?")
                {
                    mReplies.emplace_back( Data);
                }
                else if( command.starts_with( ":MEASure:") && command.back() != '?'
                         && command.find( "? ") != std::string_view::npos)
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
                    throw hal::io::TransportTimeout( "nothing queued on the fake scope");
                }

                std::string reply = mReplies.front();

                mReplies.erase( mReplies.begin());

                return reply;
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "fake DSOX1202G";
            }

            [[nodiscard]]
            auto sent() const -> const std::vector<std::string> &
            {
                return mSent;
            }

            //
            // Whether a command was sent, and what followed it -- for the
            // assertions that are about *order* rather than about content, of
            // which this driver has three that matter (see the ordering tests).
            //
            [[nodiscard]]
            auto indexOf( const std::string_view command) const -> long
            {
                for( std::size_t index = 0; index < mSent.size(); ++index)
                {
                    if( mSent[ index] == command)
                    {
                        return static_cast<long>( index);
                    }
                }

                return -1;
            }

            std::string              Identity{ "KEYSIGHT TECHNOLOGIES,DSO-X 1202G,CN12345678,01.20.2019030220" };
            std::string              Reading{  "+4.87500000E+00" };
            std::string              Preamble{ "+4,+0,+1000,+1,+1.00000000E-06,-1.00000000E-03,+0,+7.81250000E-04,+0.0,+128" };
            std::string              Data{     "#800000029+5.00E+00,+4.60E+00,+4.80E+00" };
            bool                     Armed{   true };
            bool                     Running{ false };
            std::vector<std::string> Errors;

        private:
            std::vector<std::string> mSent;
            std::vector<std::string> mReplies;
    };

    //
    // A scope with a fake transport already installed, and the fake still
    // reachable. A pair because the driver takes ownership of the transport
    // (see DSOX1202G::useTransport) and the test still needs to read what went
    // through it.
    //
    struct Attached
    {
        hal::keysight_dsox1202g::DSOX1202G Scope{ hal::InstrumentId::Osc1, hal::Simulated{} };
        FakeScope *                        Wire{ nullptr };
    };

    [[nodiscard]]
    auto attached() -> std::unique_ptr<Attached>
    {
        //
        // Simulated{} for the address, because there is genuinely no bench, plus
        // a fake transport, because the question under test is what would have
        // been sent to one. The injected transport is what wins -- see
        // DSOX1202G::isSimulated().
        //
        auto result    = std::make_unique<Attached>();
        auto transport = std::make_unique<FakeScope>();

        result->Wire = transport.get();
        result->Scope.useTransport( std::move( transport));

        return result;
    }

    //
    // The commands a session sends before anything else, dropped so that a test
    // asserting "what did this Setup send" states only the Setup.
    //
    [[nodiscard]]
    auto afterHandshake( const std::vector<std::string> & sent) -> std::vector<std::string>
    {
        std::vector<std::string> commands;

        for( const auto & command : sent)
        {
            if( command == "SYST:ERR?" || command == "*IDN?")
            {
                continue;
            }

            commands.push_back( command);
        }

        return commands;
    }
} // namespace

TEST( Dsox1202G, AnInjectedTransportIsWhatMakesADriverAttached)
{
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_TRUE( osc1.isSimulated());

    osc1.useTransport( std::make_unique<FakeScope>());

    EXPECT_FALSE( osc1.isSimulated());

    osc1.closeSession();

    EXPECT_TRUE( osc1.isSimulated());
}

TEST( Dsox1202G, ASessionDrainsTheErrorQueueAndAsksWhatItIsTalkingTo)
{
    //
    // Whatever the last user of this scope left queued is not this run's, and
    // would otherwise be reported against this run's first command. Drained
    // before *IDN? rather than after, so a stale entry cannot be mistaken for
    // the identity query having failed.
    //
    auto scope = attached();

    static_cast<void>( scope->Scope.session());

    ASSERT_GE( scope->Wire->sent().size(), 2u);
    EXPECT_EQ( scope->Wire->sent().front(), "SYST:ERR?");
    EXPECT_NE( scope->Wire->indexOf( "*IDN?"), -1);
    EXPECT_LT( scope->Wire->indexOf( "SYST:ERR?"), scope->Wire->indexOf( "*IDN?"));
}

TEST( Dsox1202G, TheIdentityCheckAcceptsTheModelHoweverItIsPunctuated)
{
    //
    // Keysight's own front panel, data sheet and firmware disagree about the
    // hyphen and the space -- "DSO-X 1202G" is what *IDN? actually answers, and
    // "DSOX1202G" is what everything else calls it. Refusing the instrument
    // this driver is for over punctuation would be the worst kind of correct.
    //
    for( const auto & identity : { "KEYSIGHT TECHNOLOGIES,DSO-X 1202G,CN12345678,01.20",
                                   "KEYSIGHT TECHNOLOGIES,DSOX1202G,CN12345678,01.20",
                                   "KEYSIGHT TECHNOLOGIES,DSO-X 1202A,CN12345678,01.20" })
    {
        auto scope = attached();

        scope->Wire->Identity = identity;

        EXPECT_NO_THROW( static_cast<void>( scope->Scope.session())) << identity;
    }
}

TEST( Dsox1202G, TheIdentityCheckRefusesAnythingElseAtThisAddress)
{
    //
    // The failure this catches is not exotic: a serial number copied from the
    // wrong row, or a VISA alias re-pointed after a re-cable, gives a meter
    // answering where the scope should be -- and every reading after that is
    // wrong in a way no criterion can see.
    //
    // The four-channel DSOX1204G is refused along with the meter, and that is
    // the interesting half: it is a real InfiniiVision that speaks nearly this
    // command set, and this driver's two-channel bound would silently confine a
    // script to half of it.
    //
    for( const auto & identity : { "Keysight Technologies,EDU34450A,MY60012345,01.00-01.00",
                                   "KEYSIGHT TECHNOLOGIES,DSO-X 1204G,CN12345678,02.00" })
    {
        auto scope = attached();

        scope->Wire->Identity = identity;

        EXPECT_THROW( static_cast<void>( scope->Scope.session()), hal::io::ScpiFault) << identity;
    }
}

TEST( Dsox1202G, ATriggerSetupSendsEdgeModeFirstAndTheLevelAfterItsSource)
{
    //
    // Two orderings, both load-bearing, both invisible in a list of commands
    // that happens to work:
    //
    //   :TRIGger:MODE EDGE     because every setting below it belongs to the
    //                          edge trigger and is merely *stored* while some
    //                          other kind is selected. A scope left in GLITch
    //                          mode by the front panel accepts all of this and
    //                          then triggers on a pulse width.
    //
    //   level after source     because the level is held per source on this
    //                          instrument, so a level sent first lands on
    //                          whichever channel was selected before.
    //
    auto scope = attached();

    setupDriver( scope->Scope.trigger()
                     .edgeSource<2>()
                     .slope( hal::keysight_dsox1202g::TriggerSlope::Falling)
                     .level( 4.8_V)
                     .sweep( hal::keysight_dsox1202g::TriggerSweep::Auto)
                     .coupling( hal::keysight_dsox1202g::TriggerCoupling::Dc)
                     .reject( hal::keysight_dsox1202g::TriggerReject::HighFrequency)
                     .holdoff( 1_ms)
                     .config());

    EXPECT_EQ( afterHandshake( scope->Wire->sent()),
               ( std::vector<std::string>{
                   ":TRIGger:MODE EDGE",
                   ":TRIGger:EDGE:SOURce CHANnel2",
                   ":TRIGger:EDGE:SLOPe NEGative",
                   ":TRIGger:EDGE:LEVel 4.8",
                   ":TRIGger:SWEep AUTO",
                   ":TRIGger:EDGE:COUPling DC",
                   ":TRIGger:EDGE:REJect HFReject",
                   ":TRIGger:HOLDoff 0.001" }));
}

TEST( Dsox1202G, ASetupSendsOnlyWhatItNamed)
{
    //
    // An unset field means "leave whatever is already configured", and on an
    // attached instrument that has to mean *sending nothing* for it -- a driver
    // that filled in its own defaults would silently overwrite settings a
    // previous Setup made.
    //
    auto scope = attached();

    setupDriver( scope->Scope.trigger().level( 2.5_V).config());

    EXPECT_EQ( afterHandshake( scope->Wire->sent()),
               ( std::vector<std::string>{ ":TRIGger:MODE EDGE", ":TRIGger:EDGE:LEVel 2.5" }));
}

TEST( Dsox1202G, AChannelSetupSendsTheProbeRatioBeforeAnythingScaledByIt)
{
    //
    // The vertical scale, the offset and the trigger level are all expressed at
    // the probe tip, so they are scaled by the attenuation factor. A scale sent
    // before the ratio is a scale for the wrong divider -- and one that is only
    // corrected when some later Setup happens to send both again.
    //
    auto scope = attached();

    setupDriver( scope->Scope.channel<2>()
                     .coupling( hal::keysight_dsox1202g::Coupling::Dc)
                     .voltsPerDivision( 100_mV)
                     .verticalOffset( 5_V)
                     .bandwidth( hal::keysight_dsox1202g::Bandwidth::Limited)
                     .probeAttenuation( 10.0)
                     .display( hal::keysight_dsox1202g::ChannelDisplay::On)
                     .config());

    EXPECT_EQ( afterHandshake( scope->Wire->sent()),
               ( std::vector<std::string>{
                   ":CHANnel2:PROBe 10",
                   ":CHANnel2:COUPling DC",
                   ":CHANnel2:SCALe 0.1",
                   ":CHANnel2:OFFSet 5",
                   ":CHANnel2:BWLimit ON",
                   ":CHANnel2:DISPlay ON" }));
}

TEST( Dsox1202G, ATimebaseSetupWidensTheWindowBeforePlacingTheTriggerInIt)
{
    //
    // :TIMebase:POSition is clamped against the current window, so a 50 ms delay
    // sent while the window is still 1 us/div is clamped and then left there
    // when the scale arrives.
    //
    auto scope = attached();

    setupDriver( scope->Scope.timebase()
                     .position( 50_ms)
                     .timePerDivision( 10_ms)
                     .reference( hal::keysight_dsox1202g::TimebaseReference::Left)
                     .config());

    EXPECT_EQ( afterHandshake( scope->Wire->sent()),
               ( std::vector<std::string>{
                   ":TIMebase:SCALe 0.01",
                   ":TIMebase:REFerence LEFT",
                   ":TIMebase:POSition 0.05" }));
}

TEST( Dsox1202G, AveragingSendsTheTypeAndThenTheCount)
{
    auto scope = attached();

    setupDriver( scope->Scope.acquisition().averagedOver( 16).config());

    EXPECT_EQ( afterHandshake( scope->Wire->sent()),
               ( std::vector<std::string>{ ":ACQuire:TYPE AVERage", ":ACQuire:COUNt 16" }));
}

TEST( Dsox1202G, ArmingFollowsTheGuidesSingleShotSequence)
{
    //
    // :STOP so nothing is racing an acquisition already running, *OPC? so the
    // stop has actually taken effect, :SINGle to arm, and :AER? until the scope
    // says it is armed and ready -- which is the moment Keysight's own example
    // marks "enable DUT here", and the moment core::ArmEngine promises a script.
    //
    auto scope = attached();

    scope->Wire->Armed = true;

    armDriver( scope->Scope.single().config());

    EXPECT_EQ( afterHandshake( scope->Wire->sent()),
               ( std::vector<std::string>{ ":STOP", "*OPC?", ":SINGle", ":AER?" }));
    EXPECT_TRUE( scope->Scope.isArmed());
}

TEST( Dsox1202G, AScopeThatNeverReportsItselfArmedIsAFailureNotAResult)
{
    //
    // The one place this driver throws rather than reporting. A capture that
    // did not trigger is a result a script checks; a scope that never armed has
    // produced no result at all, and the script is one line from causing the
    // event it cannot capture -- which would leave the previous acquisition to
    // be measured against a criterion.
    //
    auto scope = attached();

    scope->Wire->Armed = false;

    EXPECT_THROW( armDriver( scope->Scope.single().armTimeout( Time{ 0.0 }).config()),
                  hal::io::TransportTimeout);
    EXPECT_FALSE( scope->Scope.isArmed());
}

TEST( Dsox1202G, AwaitWatchesTheRunBitAndNotTheTriggerEventRegister)
{
    //
    // :TER? would answer sooner and wrongly -- it says a trigger happened, and a
    // triggered scope may still be filling its record. The RUN bit clearing is
    // the instrument saying the acquisition is over and the record can be
    // measured.
    //
    auto scope = attached();

    armDriver( scope->Scope.single().config());

    scope->Wire->Running = false;

    EXPECT_TRUE( awaitDriver( scope->Scope.single().config()));
    EXPECT_EQ( scope->Wire->sent().back(), ":OPERegister:CONDition?");
    EXPECT_EQ( scope->Wire->indexOf( ":TER?"), -1);
}

TEST( Dsox1202G, ACaptureThatNeverTriggersReportsItAndStopsTheScope)
{
    //
    // False rather than an exception: the event the script went looking for did
    // not happen, or did not cross the trigger level, and that is a verdict the
    // script's own criterion records.
    //
    // :STOP on the way out, because a scope left armed is a scope that will
    // trigger on the *next* script's stimulus -- the stale-expectation problem
    // safe() exists for, arriving through the door marked "timeout".
    //
    auto scope = attached();

    armDriver( scope->Scope.single().config());

    scope->Wire->Running = true;

    EXPECT_FALSE( awaitDriver( scope->Scope.single().timeout( Time{ 0.0 }).config()));
    EXPECT_EQ( scope->Wire->sent().back(), ":STOP");
}

TEST( Dsox1202G, AwaitingWithoutArmingSaysSoWithoutTouchingTheInstrument)
{
    auto scope = attached();

    EXPECT_FALSE( awaitDriver( scope->Scope.single().config()));
    EXPECT_TRUE( scope->Wire->sent().empty());
}

TEST( Dsox1202G, AMeasurementNamesItsSourceInTheQueryItself)
{
    //
    // ":MEASure:VBASe? CHANnel2" both selects the source and answers about it:
    // one round trip instead of two, and -- the part that matters -- one fewer
    // piece of instrument state for a later reading to inherit.
    //
    auto scope = attached();

    scope->Wire->Reading = "+4.98700000E+00";

    const auto reading = scope->Scope.channel<2>().vbase().rawMeasure();

    EXPECT_DOUBLE_EQ( reading.value(), 4.987);
    EXPECT_EQ( scope->Wire->sent().back(), ":MEASure:VBASe? CHANnel2");
}

TEST( Dsox1202G, AnUnmeasurableReadingOnTheWireIsTheSentinelAndNothingElse)
{
    //
    // +9.9E+37 is all this instrument says when it cannot make a measurement.
    // The measurement's own name is added because it is the one thing that *is*
    // known -- which question went unanswered.
    //
    auto scope = attached();

    scope->Wire->Reading = "+9.90000000E+37";

    try
    {
        static_cast<void>( scope->Scope.channel<1>().riseTime().rawMeasure());
        FAIL() << "expected core::UnmeasurableReading";
    }
    catch( const core::UnmeasurableReading & unmeasurable)
    {
        EXPECT_EQ( unmeasurable.reason(),
                   "rise time: " + std::string( hal::keysight_dsox1202g::kUnmeasurable));
    }
}

TEST( Dsox1202G, EdgeTimingSendsTheStandardThresholdsAndOnlyForEdgeMeasurements)
{
    //
    // 10/50/90 is what :MEASure:DEFine THResholds,STANdard means and what this
    // driver's riseTime() seeds, so the ordinary case sends the instrument's own
    // default back to it -- one command, and it makes the reading independent of
    // whatever the last script left behind.
    //
    auto rise = attached();

    static_cast<void>( rise->Scope.channel<1>().riseTime().rawMeasure());

    EXPECT_EQ( afterHandshake( rise->Wire->sent()),
               ( std::vector<std::string>{ ":MEASure:DEFine THResholds,STANdard,CHANnel1",
                                           ":MEASure:RISetime? CHANnel1" }));

    //
    // ...and nothing of the sort before a VMIN, which is not measured between
    // thresholds at all.
    //
    auto level = attached();

    static_cast<void>( level->Scope.channel<1>().vmin().rawMeasure());

    EXPECT_EQ( afterHandshake( level->Wire->sent()),
               ( std::vector<std::string>{ ":MEASure:VMIN? CHANnel1" }));
}

TEST( Dsox1202G, OverriddenThresholdsGoOutAsPercentages)
{
    //
    // A script that named its own pair gets PERCent, with the middle threshold
    // left at 50: this driver's ports carry an upper and a lower and nothing in
    // between, because the middle one only affects delay and phase, which are
    // not modelled.
    //
    auto scope = attached();

    static_cast<void>( scope->Scope.channel<2>().fallTime().lowThreshold( 0.2).highThreshold( 0.8).rawMeasure());

    EXPECT_EQ( afterHandshake( scope->Wire->sent()),
               ( std::vector<std::string>{ ":MEASure:DEFine THResholds,PERCent,80,50,20,CHANnel2",
                                           ":MEASure:FALLtime? CHANnel2" }));
}

TEST( Dsox1202G, ATraceComesBackScaledWithTheTimebaseFromItsPreamble)
{
    //
    // ASCii format, so the instrument has already converted its integers to
    // volts -- scaling them again here against the preamble's y fields would
    // square the vertical scale. What the preamble is read for is the *time*
    // axis: xincrement between samples, xorigin for the first one relative to
    // the trigger.
    //
    auto scope = attached();

    scope->Wire->Data = "#800000029+5.00E+00,+4.60E+00,+4.80E+00";

    const auto trace = fetchDriver( scope->Scope.channel<2>().waveform().config());

    ASSERT_EQ( trace.size(), 3u);
    EXPECT_DOUBLE_EQ( trace.at( 0), 5.0);
    EXPECT_DOUBLE_EQ( trace.at( 1), 4.6);
    EXPECT_DOUBLE_EQ( trace.at( 2), 4.8);
    EXPECT_DOUBLE_EQ( trace.timing().Increment.value(), 1e-06);
    EXPECT_DOUBLE_EQ( trace.timing().Origin.value(),  -1e-03);

    EXPECT_EQ( afterHandshake( scope->Wire->sent()),
               ( std::vector<std::string>{ ":WAVeform:SOURce CHANnel2",
                                           ":WAVeform:FORMat ASCii",
                                           ":WAVeform:PREamble?",
                                           ":WAVeform:DATA?" }));
}

TEST( Dsox1202G, ABlockHeaderIsReadRatherThanAssumed)
{
    //
    // The digit count is the character after the '#', and while this instrument
    // always sends 8, a reader who assumed it would be wrong on the first
    // instrument that does not -- and wrong silently, by one sample.
    //
    auto scope = attached();

    scope->Wire->Data = "#219+1.00E+00,+2.00E+00";

    const auto trace = fetchDriver( scope->Scope.channel<1>().waveform().config());

    ASSERT_EQ( trace.size(), 2u);
    EXPECT_DOUBLE_EQ( trace.at( 1), 2.0);
}

TEST( Dsox1202G, ATruncatedBlockIsAFaultRatherThanAShorterTrace)
{
    //
    // A reply cut short is a real failure mode, and a trace quietly missing its
    // tail is the worst way to report it: every measurement over it would be a
    // measurement of a waveform that was never captured.
    //
    auto scope = attached();

    scope->Wire->Data = "#800000099+1.00E+00,+2.00E+00";

    EXPECT_THROW( static_cast<void>( fetchDriver( scope->Scope.channel<1>().waveform().config())),
                  hal::io::ScpiFault);
}

TEST( Dsox1202G, ARejectedCommandIsReportedAgainstTheCommandThatCausedIt)
{
    //
    // The whole reason hal::io::ScpiSession::checked() exists: a SCPI instrument
    // does not answer a bad command, it queues an error and carries on. A driver
    // that did not look would leave the scope configured however it already was
    // and go on to take a perfectly plausible reading of the wrong thing.
    //
    auto scope = attached();

    //
    // The session is opened first, and the error queued after: a session drains
    // whatever it finds on connection (see the handshake test above), so an
    // error queued before that would be swallowed as somebody else's -- which
    // is exactly the behaviour that test asserts and this one would otherwise
    // silently depend on being absent.
    //
    static_cast<void>( scope->Scope.session());

    scope->Wire->Errors.push_back( "-222,\"Data out of range\"");

    EXPECT_THROW( setupDriver( scope->Scope.channel<1>().voltsPerDivision( 1000_V).config()),
                  hal::io::ScpiFault);
}

TEST( Dsox1202G, SafingStopsAnAttachedScopeAndKeepsItsSession)
{
    //
    // On an attached scope the armed flag alone would be a lie: the instrument
    // is still armed and will trigger on whatever the next script does. The
    // session is deliberately kept -- its error queue is the best evidence of
    // what went wrong on the run that just failed.
    //
    auto scope = attached();

    armDriver( scope->Scope.single().config());

    scope->Scope.safe();

    EXPECT_FALSE( scope->Scope.isArmed());
    EXPECT_EQ( scope->Wire->sent().back(), ":STOP");
    EXPECT_FALSE( scope->Scope.isSimulated());
}

TEST( Dsox1202G, SafingNeverOpensASessionOfItsOwn)
{
    //
    // Safing runs when a script has already failed, quite possibly because this
    // instrument is unreachable. Opening a session at that moment would replace
    // the run's real failure with a transport error from the cleanup path.
    //
    // A Simulated address has nothing to open, so this asserts the same rule the
    // only way a test without a bench can: it must not throw, and there must be
    // nothing to talk to afterwards either.
    //
    hal::keysight_dsox1202g::DSOX1202G osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    armDriver( osc1.single().config());

    EXPECT_NO_THROW( osc1.safe());
    EXPECT_TRUE( osc1.isSimulated());
    EXPECT_FALSE( osc1.isArmed());
}

TEST( Dsox1202G, AnArmThatCouldNotBeSentLeavesTheScopeUnarmed)
{
    //
    // The failure this guards is quiet rather than loud: a driver that set the
    // armed flag before talking, and then failed to talk, would hand the next
    // Await a capture that was never taken -- and Await answers for the flag,
    // not for the instrument.
    //
    // A refused :SINGle stands in for the whole class here (an unreachable
    // scope and an :AER? that never goes to 1 are the other two), because it is
    // the one a fake can produce without waiting for anything.
    //
    auto scope = attached();

    static_cast<void>( scope->Scope.session());

    scope->Wire->Errors.push_back( "-113,\"Undefined header\"");

    EXPECT_THROW( armDriver( scope->Scope.single().config()), hal::io::ScpiFault);
    EXPECT_FALSE( scope->Scope.isArmed());
    EXPECT_FALSE( awaitDriver( scope->Scope.single().config()));
}
