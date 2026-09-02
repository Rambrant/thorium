//
// hal::keysight_dsox1202g::DSOX1202G's own tests.
//
// Written against the scope on the bench rather than ported wholesale from
// instruments/keysight_dso8064a/tests, and the differences between the two
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
