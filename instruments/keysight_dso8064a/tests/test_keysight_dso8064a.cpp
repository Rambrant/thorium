//
// hal::keysight_dso8064a::DSO8064A's own tests, moved here verbatim from framework/hal/tests/
// test_instrument.cpp when this driver moved out of framework/hal into its own
// directory -- the same lift hal::keysight_l4411a::L4411A's tests got, and for the same reason.
// Test names are unchanged (still the HalInstrument suite) so the move is
// provably behaviour-preserving: the same named tests, in a different binary.
//
// hal::InstrumentId and to_string() over it stay behind in the original file:
// they are hal's own generic mechanism and exist without reference to any
// particular instrument.
//
#include "hal/keysight_dso8064a.hpp"

#include <gtest/gtest.h>

#include <concepts>

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape rig/tests/test_safing.cpp
// uses for hal::SafeableInstrument, and hal/tests/driver/test_address.cpp for the
// hal::ReachableOver mechanism itself).
//
// All three remote interfaces are on this scope's back panel; a PC serial
// port is not one of them.
//
namespace
{
    static_assert(   std::constructible_from< hal::keysight_dso8064a::DSO8064A, hal::InstrumentId, hal::Gpib> );
    static_assert(   std::constructible_from< hal::keysight_dso8064a::DSO8064A, hal::InstrumentId, hal::Lan> );
    static_assert(   std::constructible_from< hal::keysight_dso8064a::DSO8064A, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< hal::keysight_dso8064a::DSO8064A, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::keysight_dso8064a::DSO8064A, hal::InstrumentId, hal::Serial> );
    static_assert( ! std::constructible_from< hal::keysight_dso8064a::DSO8064A, hal::InstrumentId> );
} // namespace

using namespace core::literals;
using namespace core::quantities;

namespace
{
    using namespace core::literals;
    using namespace core::quantities;

    //
    // ValidDso8064aChannel's compile-time bound, checked the concept-wrapped
    // way -- see core/tests/criteria/test_static_constraints.cpp's own comment for
    // why a bare static_assert(!requires{...}) is unreliable and this
    // routing-through-a-concept form isn't.
    //
    template<unsigned N>
    concept CanChannel = requires( hal::keysight_dso8064a::DSO8064A & osc) { osc.template channel<N>(); };

    static_assert(  CanChannel<1> );
    static_assert(  CanChannel<4> );
    static_assert( !CanChannel<0> );
    static_assert( !CanChannel<5> );
} // namespace

TEST( HalInstrument, DSO8064AVppPortReturnsSimulatedReading)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);

    auto port = osc1.channel<1>().vpp();

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 3.3);
    EXPECT_EQ( port.instrumentId(), hal::InstrumentId::Osc1);
}

TEST( HalInstrument, DSO8064APortOutlivesTheTemporaryChannelViewThatCreatedIt)
{
    // The dangling-reference trap an earlier version of this file had:
    // channel<1>() returns a temporary DSO8064AChannel<1>, gone by the next
    // statement -- Port must bind to the real, long-lived DSO8064A&, not to
    // that temporary, or this reads garbage (or crashes) instead of 3.3.
    // See hal/keysight_dso8064a.hpp's own comment on DSO8064AChannel for why.
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);

    auto port = osc1.channel<1>().vpp();  // the channel view temporary is gone after this line

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 3.3);  // still valid here
}

TEST( HalInstrument, DSO8064AExposesTheWholeAmplitudeFamily)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVmax( 1, 1.9_V);
    osc1.setSimulatedVmin( 1, -1.4_V);
    osc1.setSimulatedVrms( 1, 1.2_V);
    osc1.setSimulatedVaverage( 1, 0.1_V);

    auto ch1 = osc1.channel<1>();

    EXPECT_DOUBLE_EQ( ch1.vpp().rawMeasure().value(),      3.3);
    EXPECT_DOUBLE_EQ( ch1.vmax().rawMeasure().value(),     1.9);
    EXPECT_DOUBLE_EQ( ch1.vmin().rawMeasure().value(),    -1.4);
    EXPECT_DOUBLE_EQ( ch1.vrms().rawMeasure().value(),     1.2);
    EXPECT_DOUBLE_EQ( ch1.vaverage().rawMeasure().value(), 0.1);
}

TEST( HalInstrument, DSO8064AChannelsAreIndependentlyAddressedSimulatedData)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVpp( 3, 5.0_V);

    EXPECT_DOUBLE_EQ( osc1.channel<1>().vpp().rawMeasure().value(), 3.3);
    EXPECT_DOUBLE_EQ( osc1.channel<3>().vpp().rawMeasure().value(), 5.0);
}

TEST( HalInstrument, DSO8064AExposesTheTimingFamily)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedFrequency( 3, 1.0_kHz);
    osc1.setSimulatedPeriod( 3, 1.0_ms);
    osc1.setSimulatedRiseTime( 3, Time{ 12e-9});
    osc1.setSimulatedFallTime( 3, Time{ 14e-9});

    auto ch3 = osc1.channel<3>();

    EXPECT_DOUBLE_EQ( ch3.frequency().rawMeasure().value(), 1000.0);
    EXPECT_DOUBLE_EQ( ch3.period().rawMeasure().value(),    0.001);
    EXPECT_DOUBLE_EQ( ch3.riseTime().rawMeasure().value(),  12e-9);
    EXPECT_DOUBLE_EQ( ch3.fallTime().rawMeasure().value(),  14e-9);
}

TEST( HalInstrument, DSO8064ARiseTimeDefaultsToTenNinetyThresholds)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    auto port = osc1.channel<1>().riseTime();

    ASSERT_TRUE( port.setup().LowThreshold.has_value());
    ASSERT_TRUE( port.setup().HighThreshold.has_value());
    EXPECT_DOUBLE_EQ( *port.setup().LowThreshold,  0.1);
    EXPECT_DOUBLE_EQ( *port.setup().HighThreshold, 0.9);
}

TEST( HalInstrument, DSO8064ARiseTimeThresholdsAreOverridable)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    auto port = osc1.channel<1>().riseTime().lowThreshold( 0.2).highThreshold( 0.8);

    EXPECT_DOUBLE_EQ( *port.setup().LowThreshold,  0.2);
    EXPECT_DOUBLE_EQ( *port.setup().HighThreshold, 0.8);
}

TEST( HalInstrument, DSO8064ADefaultsToVppMode)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_EQ( osc1.mode(), hal::keysight_dso8064a::DSO8064A::Mode::Vpp);
}

TEST( HalInstrument, DSO8064AModeIsSharedAcrossPortHandlesHeldPastAModeSwitch)
{
    // Documents the same known, accepted sharp edge as hal::keysight_l4411a::L4411A's own
    // test: a port handle obtained before a mode switch still reads
    // whichever mode is current when rawMeasure() is eventually called, not
    // the mode active when the handle was created.
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVrms( 1, 1.2_V);

    auto vppPort = osc1.channel<1>().vpp();
    (void)osc1.channel<1>().vrms();

    EXPECT_DOUBLE_EQ( vppPort.rawMeasure().value(), 1.2);
}

TEST( HalInstrument, DSO8064AChannelIsSharedAcrossPortHandlesHeldPastAChannelSwitch)
{
    // Same sharp edge, other axis: channel is instrument-level state too
    // (see hal/keysight_dso8064a.hpp's own comment on DSO8064AChannel for why), so a
    // port handle obtained on channel 1 still reads whichever channel is
    // current at rawMeasure() time if channel 3 gets selected afterward.
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVpp( 3, 5.0_V);

    auto ch1Port = osc1.channel<1>().vpp();
    (void)osc1.channel<3>().vpp();

    EXPECT_DOUBLE_EQ( ch1Port.rawMeasure().value(), 5.0);
}

//
// ---------------------------------------------------------------------
// The Setup surface
// ---------------------------------------------------------------------
//

TEST( HalInstrument, DSO8064ATriggerSetupRecordsEveryFieldItWasGiven)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.trigger()
                          .edgeSource<3>()
                          .slope( hal::keysight_dso8064a::TriggerSlope::Falling)
                          .level( -0.5_V)
                          .sweep( hal::keysight_dso8064a::TriggerSweep::Auto)
                          .coupling( hal::keysight_dso8064a::TriggerCoupling::Dc)
                          .holdoff( 1_ms)
                          .config());

    EXPECT_EQ( osc1.triggerSource(),   3u);
    EXPECT_EQ( osc1.triggerSlope(),    hal::keysight_dso8064a::TriggerSlope::Falling);
    EXPECT_EQ( osc1.triggerSweep(),    hal::keysight_dso8064a::TriggerSweep::Auto);
    EXPECT_EQ( osc1.triggerCoupling(), hal::keysight_dso8064a::TriggerCoupling::Dc);
    ASSERT_TRUE( osc1.triggerLevel().has_value());
    EXPECT_DOUBLE_EQ( osc1.triggerLevel()->value(), -0.5);
    ASSERT_TRUE( osc1.triggerHoldoff().has_value());
    EXPECT_DOUBLE_EQ( osc1.triggerHoldoff()->value(), 0.001);
}

TEST( HalInstrument, DSO8064AASetupLeavesFieldsItDidNotNameAlone)
{
    //
    // The "unset means leave what is already configured" convention, which is
    // the whole reason every config field is optional -- see
    // hal/keysight_dso8064a.hpp. A second Setup naming only the level must not reset
    // the slope that the first one chose.
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.trigger().slope( hal::keysight_dso8064a::TriggerSlope::Falling).level( -0.5_V).config());
    setupDriver( osc1.trigger().level( -1.5_V).config());

    EXPECT_EQ( osc1.triggerSlope(), hal::keysight_dso8064a::TriggerSlope::Falling);
    EXPECT_DOUBLE_EQ( osc1.triggerLevel()->value(), -1.5);
}

TEST( HalInstrument, DSO8064ATimebaseAndAcquisitionAreSeparateSubsystems)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.timebase()
                          .timePerDivision( 10_ms)
                          .reference( hal::keysight_dso8064a::TimebaseReference::Left)
                          .config());

    setupDriver( osc1.acquisition()
                          .mode( hal::keysight_dso8064a::AcquisitionMode::HighResolution)
                          .automaticPoints()
                          .config());

    ASSERT_TRUE( osc1.timePerDivision().has_value());
    EXPECT_DOUBLE_EQ( osc1.timePerDivision()->value(), 0.01);
    EXPECT_EQ( osc1.timebaseReference(), hal::keysight_dso8064a::TimebaseReference::Left);
    EXPECT_EQ( osc1.acquisitionMode(),   hal::keysight_dso8064a::AcquisitionMode::HighResolution);
    EXPECT_EQ( osc1.automaticPoints(),   true);
}

TEST( HalInstrument, DSO8064AAnExplicitMemoryDepthTurnsTheAutomaticChoiceOff)
{
    // points( n) and automaticPoints() are not independent -- see the
    // builder's own comment. Asking for exactly a million points while
    // leaving the scope free to choose is not a coherent instruction.
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.acquisition().automaticPoints().config());
    setupDriver( osc1.acquisition().points( 1'000'000).config());

    EXPECT_EQ( osc1.points(),          1'000'000u);
    EXPECT_EQ( osc1.automaticPoints(), false);
}

TEST( HalInstrument, DSO8064AAveragingCarriesItsCount)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.acquisition().averagedOver( 16).config());

    EXPECT_EQ( osc1.averaging(),    true);
    EXPECT_EQ( osc1.averageCount(), 16u);

    setupDriver( osc1.acquisition().unaveraged().config());

    EXPECT_EQ( osc1.averaging(), false);
}

TEST( HalInstrument, DSO8064AChannelSetupAppliesToTheNamedChannelOnly)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    setupDriver( osc1.channel<3>()
                          .input( hal::keysight_dso8064a::ChannelInput::Dc1M)
                          .voltsPerDivision( 100_mV)
                          .verticalOffset( -4.5_V)
                          .bandwidth( hal::keysight_dso8064a::Bandwidth::Limited)
                          .probeAdapter( hal::keysight_dso8064a::ProbeAdapter::Div10)
                          .display( hal::keysight_dso8064a::ChannelDisplay::On)
                          .config());

    EXPECT_EQ( osc1.channelInput( 3),     hal::keysight_dso8064a::ChannelInput::Dc1M);
    EXPECT_EQ( osc1.channelBandwidth( 3), hal::keysight_dso8064a::Bandwidth::Limited);
    EXPECT_EQ( osc1.probeAdapter( 3),     hal::keysight_dso8064a::ProbeAdapter::Div10);
    EXPECT_EQ( osc1.channelDisplay( 3),   hal::keysight_dso8064a::ChannelDisplay::On);
    ASSERT_TRUE( osc1.voltsPerDivision( 3).has_value());
    EXPECT_DOUBLE_EQ( osc1.voltsPerDivision( 3)->value(), 0.1);
    EXPECT_DOUBLE_EQ( osc1.verticalOffset( 3)->value(), -4.5);

    // Channel 1 was never named and must be untouched.
    EXPECT_FALSE( osc1.channelInput( 1).has_value());
    EXPECT_FALSE( osc1.voltsPerDivision( 1).has_value());
}

namespace
{
    //
    // A channel view is a temporary, and the builder it returns outlives it --
    // the same trap core::Port had (see hal/keysight_dso8064a.hpp on
    // DSO8064AChannelBuilder). A builder holding DSO8064AChannel<N>& rather
    // than the instrument and the number would dangle here.
    //
    // Checked as a compile-time property too: a builder must be constructible
    // from the instrument and a plain number, never from the view.
    //
    static_assert( std::constructible_from< hal::keysight_dso8064a::ChannelBuilder, hal::keysight_dso8064a::DSO8064A &, unsigned> );
} // namespace

TEST( HalInstrument, DSO8064AChannelBuilderOutlivesTheTemporaryChannelViewThatCreatedIt)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto builder = osc1.channel<2>().voltsPerDivision( 50_mV);   // view is gone after this line

    setupDriver( builder.config());

    ASSERT_TRUE( osc1.voltsPerDivision( 2).has_value());
    EXPECT_DOUBLE_EQ( osc1.voltsPerDivision( 2)->value(), 0.05);
}

namespace
{
    //
    // The trigger source is bounded the same way channel<N>() is -- checked
    // through a concept rather than as a bare static_assert(!requires{...}),
    // see core/tests/criteria/test_static_constraints.cpp for why that distinction
    // matters.
    //
    template<unsigned N>
    concept CanTriggerOn = requires( hal::keysight_dso8064a::TriggerBuilder & trigger) { trigger.template edgeSource<N>(); };

    static_assert(  CanTriggerOn<1> );
    static_assert(  CanTriggerOn<4> );
    static_assert( !CanTriggerOn<0> );
    static_assert( !CanTriggerOn<5> );

    //
    // A scope has no output to energise, so Apply/Remove on one is "no
    // matching function" rather than a call that quietly does nothing -- the
    // same absence hal::racal1260::Racal1260 relies on, checked in the same way. Setup,
    // which it does answer to, is checked alongside so this proves a
    // distinction rather than that nothing compiles.
    //
    template<typename ConfigT>
    concept HasApplyDriver = requires( const ConfigT & config) { applyDriver( config); };

    static_assert( !HasApplyDriver< hal::keysight_dso8064a::TriggerConfig> );
    static_assert( !HasApplyDriver< hal::keysight_dso8064a::ChannelConfig> );

    //
    // ...and Arm/Await are answered for by the capture config and by nothing
    // else on this instrument: Arm( Osc1.trigger()) is a compile error.
    //
    template<typename ConfigT>
    concept HasArmDriver = requires( const ConfigT & config) { armDriver( config); };

    static_assert(  HasArmDriver< hal::keysight_dso8064a::SingleConfig> );
    static_assert( !HasArmDriver< hal::keysight_dso8064a::TriggerConfig> );
} // namespace

namespace
{
    //
    // `Setup( Osc1.channel<3>())` -- a Setup naming a channel and no setting,
    // which can only be a mistake -- has to be a compile error rather than a
    // call that does nothing. That is exactly the absence of config() on the
    // channel view, and it is asserted in both directions here: the builder
    // every setting method returns does have one.
    //
    // Routed through a concept rather than written as a bare
    // static_assert(!requires{...}), because a requires-expression naming a
    // member of a known, non-dependent type is a hard error rather than a
    // false answer -- see core/tests/criteria/test_static_constraints.cpp.
    //
    template<typename T>
    concept HasConfig = requires( T & subject) { subject.config(); };

    static_assert(  HasConfig< hal::keysight_dso8064a::ChannelBuilder> );
    static_assert( !HasConfig< hal::keysight_dso8064a::Channel<3>> );
} // namespace

//
// ---------------------------------------------------------------------
// Arm and Await
// ---------------------------------------------------------------------
//

TEST( HalInstrument, DSO8064AArmingLeavesTheScopeArmedAndTheCaptureCompletes)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_FALSE( osc1.isArmed());

    armDriver( osc1.single().timeout( 2_s).config());

    EXPECT_TRUE( osc1.isArmed());
    EXPECT_TRUE( awaitDriver( osc1.single().config()));
    EXPECT_FALSE( osc1.isArmed());
    EXPECT_TRUE( osc1.lastAcquisitionCompleted());
}

TEST( HalInstrument, DSO8064AACaptureThatNeverTriggersReportsNotCompleted)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedCaptureCompletes( false);

    armDriver( osc1.single().config());

    EXPECT_FALSE( awaitDriver( osc1.single().config()));
    EXPECT_FALSE( osc1.lastAcquisitionCompleted());
}

TEST( HalInstrument, DSO8064AAwaitingWithoutArmingReportsNotCompleted)
{
    //
    // A script that measures a transient without arming a capture has
    // measured whatever was left in the acquisition buffer. That is a wrong
    // answer, and this is the check that catches it -- so it answers false
    // rather than throwing and abandoning the rest of the run. See
    // DSO8064A::awaitAcquisition's own comment.
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_FALSE( awaitDriver( osc1.single().config()));
}

TEST( HalInstrument, DSO8064AArmingClearsAnyPreviousCompletion)
{
    //
    // The ":ADER? // clear ADER event" line in Keysight's own single-shot
    // sequence, as behaviour: a second capture must not be answered by the
    // first one's done event.
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    armDriver( osc1.single().config());
    EXPECT_TRUE( awaitDriver( osc1.single().config()));

    armDriver( osc1.single().config());

    EXPECT_FALSE( osc1.lastAcquisitionCompleted());
}

TEST( HalInstrument, DSO8064ASafingDisarmsAPendingCapture)
{
    //
    // A scope left armed after a script died is waiting for an event that is
    // no longer coming, and the next script's Await would be answered by it.
    // Settings are deliberately NOT reset -- see DSO8064A::safe().
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

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

TEST( HalInstrument, DSO8064AAnUnmeasurableReadingThrowsCarryingTheInstrumentsOwnReason)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedUnmeasurable( 3, hal::keysight_dso8064a::DSO8064A::Mode::RiseTime,
                                   hal::keysight_dso8064a::MeasurementFault::RequiredEdgeNotFound);

    auto port = osc1.channel<3>().riseTime();

    try
    {
        (void)port.rawMeasure();
        FAIL() << "expected core::UnmeasurableReading";
    }
    catch( const core::UnmeasurableReading & unmeasurable)
    {
        EXPECT_EQ( unmeasurable.reason(), "required edge not found");
    }
}

TEST( HalInstrument, DSO8064AAFaultIsPerMeasurementNotPerChannel)
{
    //
    // A clipped trace still has a perfectly good period, and a flat trace
    // with no edge on it has a vmax and no rise time. A single
    // "this channel is broken" flag could express neither.
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVmax( 2, 4.8_V);
    osc1.setSimulatedRiseTime( 2, Time{ 12e-9});
    osc1.setSimulatedUnmeasurable( 2, hal::keysight_dso8064a::DSO8064A::Mode::RiseTime,
                                   hal::keysight_dso8064a::MeasurementFault::WaveformClippedHigh);

    EXPECT_DOUBLE_EQ( osc1.channel<2>().vmax().rawMeasure().value(), 4.8);
    EXPECT_THROW( (void)osc1.channel<2>().riseTime().rawMeasure(), core::UnmeasurableReading);
}

TEST( HalInstrument, DSO8064AAFaultCanBeCleared)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVmin( 1, -0.4_V);
    osc1.setSimulatedUnmeasurable( 1, hal::keysight_dso8064a::DSO8064A::Mode::Vmin, hal::keysight_dso8064a::MeasurementFault::NoDataOnScreen);

    EXPECT_THROW( (void)osc1.channel<1>().vmin().rawMeasure(), core::UnmeasurableReading);

    osc1.clearSimulatedUnmeasurable( 1, hal::keysight_dso8064a::DSO8064A::Mode::Vmin);

    EXPECT_DOUBLE_EQ( osc1.channel<1>().vmin().rawMeasure().value(), -0.4);
}

TEST( HalInstrument, DSO8064AExposesTheBaselineFamilyTheTransientCalculationNeeds)
{
    //
    // vbase() is not vmin(): base is the settled level the waveform spends
    // its time at, min is the most extreme sample in the record. Their
    // difference is the size of the negative transient -- the calculation the
    // legacy ATE script assembled by hand out of a screen median and a
    // manually subtracted vertical offset.
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVbase( 3, 5.0_V);
    osc1.setSimulatedVtop( 3, 5.1_V);
    osc1.setSimulatedVmin( 3, 4.62_V);
    osc1.setSimulatedVamplitude( 3, 0.1_V);
    osc1.setSimulatedVmiddle( 3, 5.05_V);

    auto ch3 = osc1.channel<3>();

    EXPECT_DOUBLE_EQ( ch3.vbase().rawMeasure().value(),      5.0);
    EXPECT_DOUBLE_EQ( ch3.vtop().rawMeasure().value(),       5.1);
    EXPECT_DOUBLE_EQ( ch3.vamplitude().rawMeasure().value(), 0.1);
    EXPECT_DOUBLE_EQ( ch3.vmiddle().rawMeasure().value(),    5.05);

    const auto transient = ch3.vbase().rawMeasure() - ch3.vmin().rawMeasure();

    EXPECT_NEAR( transient.value(), 0.38, 1e-9);
}

TEST( HalInstrument, DSO8064AExposesPulseWidths)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedPositiveWidth( 4, 20_us);
    osc1.setSimulatedNegativeWidth( 4, 30_us);

    auto ch4 = osc1.channel<4>();

    EXPECT_DOUBLE_EQ( ch4.positiveWidth().rawMeasure().value(), 20e-6);
    EXPECT_DOUBLE_EQ( ch4.negativeWidth().rawMeasure().value(), 30e-6);
}

//
// ---------------------------------------------------------------------
// What the run journal is told
// ---------------------------------------------------------------------
//

TEST( HalInstrument, DSO8064ADescribesOnlyTheSettingsASetupNamed)
{
    //
    // A Setup that named only the trigger level is a different instruction
    // from one that named the whole trigger, and a rendering that filled in
    // the rest would be inventing settings the script never chose. Same
    // reasoning hal::racal1260::Racal1260's describeConfig gives about "9600 8N1".
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto described = describeConfig(
        osc1.trigger().edgeSource<3>().slope( hal::keysight_dso8064a::TriggerSlope::Falling).config());

    EXPECT_EQ( described.Instrument, "Osc1");
    EXPECT_EQ( described.Settings,   "trigger.source=3, trigger.slope=Falling");
}

TEST( HalInstrument, DSO8064AChannelSettingsAreDescribedAgainstTheirOwnChannel)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto described = describeConfig(
        osc1.channel<3>().input( hal::keysight_dso8064a::ChannelInput::Dc1M).voltsPerDivision( 100_mV).config());

    EXPECT_EQ( described.Settings, "ch3.input=Dc1M, ch3.perDiv=100 mV");
}

TEST( HalInstrument, DSO8064AACaptureAlwaysDescribesTheTimeoutsItWillUse)
{
    //
    // The one place an unset field is filled in for the log, and deliberately
    // so: a timeout is the number that decides how a failing capture behaves,
    // so a report of a run that timed out has to say what it was waiting for.
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto defaulted = describeConfig( osc1.single().config());
    const auto named     = describeConfig( osc1.single().timeout( 2_s).config());

    EXPECT_EQ( defaulted.Settings, "single.timeout=5 s, single.armTimeout=1 s");
    EXPECT_EQ( named.Settings,     "single.timeout=2 s, single.armTimeout=1 s");
}

//
// Waveform transfer -- the :WAVeform half, for core::FetchEngine. See
// core/verbs/trace.hpp for the verb and core::Waveform for what comes back.
//

TEST( HalInstrument, DSO8064AHandsBackTheCapturedRecordOffTheNamedChannel)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto trace = core::Waveform{
        core::quantityKindOf<core::quantities::Voltage>(),
        core::Waveform::Timing{ -1_ms, core::quantities::Time{ 1e-06 } },
        std::vector<double>{ 5.0, 4.6, 4.8, 5.0 } };

    osc1.setSimulatedTrace( 3, trace);

    EXPECT_EQ( fetchDriver( osc1.channel<3>().waveform().config()), trace);
}

TEST( HalInstrument, DSO8064AAnsweringAChannelNothingCapturedGivesAnEmptyTrace)
{
    //
    // Empty rather than an exception, for the reason awaitAcquisition answers
    // false rather than throwing: a script reading out a record it never
    // captured has not crashed, and the check it feeds is where that surfaces.
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_TRUE( fetchDriver( osc1.channel<2>().waveform().config()).empty());
}

TEST( HalInstrument, DSO8064AFilesEachChannelsTraceUnderItsOwnSessionKey)
{
    //
    // Four channels hold four records at once, so one slot for all of them
    // would let an injected channel-1 trace answer a channel-3 Fetch -- the
    // collision core::Port::qualifiedBy already prevents among this scope's
    // fifteen measurements.
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_EQ( traceQualifier( osc1.channel<1>().waveform().config()), "Channel1");
    EXPECT_EQ( traceQualifier( osc1.channel<3>().waveform().config()), "Channel3");
}

TEST( HalInstrument, DSO8064AWaveformBuilderCarriesItsChannelByValue)
{
    //
    // Unlike the fifteen measurement methods, this does NOT switch the
    // instrument's selected channel -- so the sharp edge they carry (a handle
    // taken before a later switch reads the later channel) does not exist here.
    //
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    osc1.setSimulatedTrace( 1, core::Waveform{
        core::quantityKindOf<core::quantities::Voltage>(),
        core::Waveform::Timing{},
        std::vector<double>{ 3.3 } });

    const auto channelOne = osc1.channel<1>().waveform();

    (void) osc1.channel<3>().vpp();   // would switch the instrument for a Measure

    ASSERT_EQ( fetchDriver( channelOne.config()).size(), 1u);
    EXPECT_DOUBLE_EQ( fetchDriver( channelOne.config()).at( 0), 3.3);
}

TEST( HalInstrument, DSO8064AATraceIsDescribedByWhichChannelItCameOff)
{
    hal::keysight_dso8064a::DSO8064A osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    const auto described = describeConfig( osc1.channel<4>().waveform().config());

    EXPECT_EQ( described.Instrument, "Osc1");
    EXPECT_EQ( described.Settings,   "ch4");
}
