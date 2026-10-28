//
// hal::pico_5244d::Oscilloscope's own tests.
//
// There is no PicoSDK here and no scope, so the library is a fake behind
// hal::pico_5244d::Ps5000a: it logs every call, answers what a 5244D would,
// and fills the capture buffers from a function the test supplies. What these
// tests can establish is the *sequence* the driver issues and the arithmetic
// around it -- unit conversions, the timebase search, how a plan becomes
// pre/post samples, what is and is not measured. What they cannot is whether
// the real library behaves as the Programmer's Guide says; the README lists
// what has to be confirmed on a bench.
//
#include "hal/pico_5244d.hpp"

#include "core/meta.hpp"
#include "hal/verbs/preflight.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <functional>
#include <string>
#include <vector>

namespace
{
    static_assert(   std::constructible_from< hal::pico_5244d::Oscilloscope, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< hal::pico_5244d::Oscilloscope, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::pico_5244d::Oscilloscope, hal::InstrumentId, hal::Lan> );
    static_assert( ! std::constructible_from< hal::pico_5244d::Oscilloscope, hal::InstrumentId, hal::Gpib> );
    static_assert( ! std::constructible_from< hal::pico_5244d::Oscilloscope, hal::InstrumentId, hal::Serial> );

    template<unsigned N>
    concept CanChannel = requires( hal::pico_5244d::Oscilloscope & osc) { osc.template channel<N>(); };

    static_assert(  CanChannel<1> );
    static_assert(  CanChannel<2> );
    static_assert( !CanChannel<0> );
    static_assert( !CanChannel<3> );

    // This driver has no trigger coupling, holdoff or reject: its trigger is a
    // comparator. A builder offering them would accept settings it ignores.
    template<typename B>
    concept HasCoupling = requires( B b) { b.coupling( hal::pico_5244d::Coupling::Dc); };

    static_assert( !HasCoupling<hal::pico_5244d::TriggerBuilder> );
    static_assert(  HasCoupling<hal::pico_5244d::ChannelBuilder> );

    //
    // What the verbs find by ADL on a builder's config. Setup, Arm, Await and
    // Fetch are templates in core/verbs/ that call these unqualified, so a
    // missing one is a compile error in a *script*, far from here; asserting
    // them here puts it next to the driver that forgot.
    //
    template<typename ConfigT>
    concept SetupConfig = requires( const ConfigT & c) { setupDriver( c); describeConfig( c); };

    template<typename ConfigT>
    concept ArmConfig = requires( const ConfigT & c) { armDriver( c); { awaitDriver( c) } -> std::same_as<bool>; describeConfig( c); };

    template<typename ConfigT>
    concept FetchConfig = requires( const ConfigT & c) { { fetchDriver( c) } -> std::same_as<core::Waveform>; traceQualifier( c); describeConfig( c); };

    static_assert( SetupConfig<hal::pico_5244d::TriggerConfig> );
    static_assert( SetupConfig<hal::pico_5244d::TimebaseConfig> );
    static_assert( SetupConfig<hal::pico_5244d::AcquisitionConfig> );
    static_assert( SetupConfig<hal::pico_5244d::ChannelConfig> );
    static_assert( ArmConfig<hal::pico_5244d::SingleConfig> );
    static_assert( FetchConfig<hal::pico_5244d::WaveformConfig> );

    // And what preflight and safing need of any row (hal/verbs/preflight.hpp).
    static_assert( hal::SafeableInstrument<hal::pico_5244d::Oscilloscope> );
    static_assert( hal::AddressableInstrument<hal::pico_5244d::Oscilloscope> );
    static_assert( hal::ContactableInstrument<hal::pico_5244d::Oscilloscope> );

    // The generator face: a source on the scope's own back panel.
    static_assert(   std::constructible_from< hal::pico_5244d::WaveformGenerator, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< hal::pico_5244d::WaveformGenerator, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::pico_5244d::WaveformGenerator, hal::InstrumentId, hal::Lan> );
    static_assert( hal::SafeableInstrument<hal::pico_5244d::WaveformGenerator> );
    static_assert( hal::AddressableInstrument<hal::pico_5244d::WaveformGenerator> );
    static_assert( hal::ContactableInstrument<hal::pico_5244d::WaveformGenerator> );

    // A builder offers only what its shape has: this generator has no duty
    // cycle, a DC level has no frequency or amplitude, and there is no load.
    template<typename B>
    concept HasFrequency = requires( B b) { b.frequency( core::quantities::Frequency{ 1000.0 }); };
    template<typename B>
    concept HasAmplitude = requires( B b) { b.amplitude( core::quantities::Voltage{ 1.0 }); };
    template<typename B>
    concept HasDutyCycle = requires( B b) { b.dutyCycle( core::quantities::Percentage{ 50.0 }); };
    template<typename B>
    concept HasInto = requires( B b) { b.into( 0); };

    using SineBuilder = hal::pico_5244d::GeneratorBuilder<hal::pico_5244d::Sine>;
    using DcBuilder   = hal::pico_5244d::GeneratorBuilder<hal::pico_5244d::Dc>;
    using NoiseBuilder = hal::pico_5244d::GeneratorBuilder<hal::pico_5244d::Noise>;

    static_assert(  HasFrequency<SineBuilder> );
    static_assert(  HasAmplitude<SineBuilder> );
    static_assert( !HasDutyCycle<SineBuilder> );
    static_assert( !HasInto<SineBuilder> );
    static_assert( !HasFrequency<DcBuilder> );
    static_assert( !HasAmplitude<DcBuilder> );
    static_assert( !HasFrequency<NoiseBuilder> );
    static_assert(  HasAmplitude<NoiseBuilder> );

    template<typename ConfigT>
    concept SourceConfig = requires( const ConfigT & c) { applyDriver( c); removeDriver( c); { isEnergised( c) } -> std::same_as<bool>; describeConfig( c); };

    static_assert( SourceConfig<hal::pico_5244d::GeneratorConfig<hal::pico_5244d::Sine>> );
    static_assert( SourceConfig<hal::pico_5244d::GeneratorConfig<hal::pico_5244d::Dc>> );
} // namespace

using namespace core::literals;
using namespace core::quantities;
using hal::pico_5244d::Status;

namespace
{
    [[nodiscard]]
    auto anyId() -> hal::InstrumentId
    {
        return core::meta::values<hal::InstrumentId>[ 0];
    }

    //
    // A 5244D, as far as the driver can tell. Timebases below 3 are refused and
    // from 3 the interval is (n-2) * 8 ns -- the 8-bit table's shape, which is
    // all the search needs to be exercised (monotone, with a first valid n).
    //
    class FakeScope final : public hal::pico_5244d::Ps5000a
    {
        public:
            std::vector<std::string>   Calls;

            std::string                Variant{ "5244D" };
            std::string                Serial{ "JO123/0456" };
            Status                     OpenStatus{ 0 };
            Status                     PowerStatus{ 0 };
            std::int16_t               AdcMax{ 32512 };
            std::int32_t               MaxSamples{ 4000000 };
            float                      OffsetLimit{ 10000.0F };

            bool                       Ready{ true };
            std::int16_t               Overflow{ 0 };

            // The ADC code for channel (0-based) sample i.
            std::function<std::int16_t( int, std::size_t)> Adc{ []( int, std::size_t) { return std::int16_t{ 0 }; } };

            [[nodiscard]]
            auto count( const std::string & prefix) const -> int
            {
                return static_cast<int>( std::ranges::count_if( Calls, [ & ]( const std::string & call)
                                                                { return call.starts_with( prefix); }));
            }

            [[nodiscard]]
            auto last( const std::string & prefix) const -> std::string
            {
                for( auto it = Calls.rbegin(); it != Calls.rend(); ++it)
                {
                    if( it->starts_with( prefix))
                    {
                        return *it;
                    }
                }

                return {};
            }

            auto enumerateUnits( std::string & serials) -> Status override
            {
                Calls.emplace_back( "enumerate");
                serials = Serial;
                return 0;
            }

            auto openUnit( std::int16_t & handle, const std::string & serial, const std::int32_t resolution) -> Status override
            {
                Calls.push_back( "open(" + serial + "," + std::to_string( resolution) + ")");
                handle = 7;
                return OpenStatus;
            }

            auto closeUnit( const std::int16_t) -> Status override { Calls.emplace_back( "close"); return 0; }

            auto changePowerSource( const std::int16_t, const Status state) -> Status override
            {
                Calls.push_back( "power(" + std::to_string( state) + ")");
                return PowerStatus;
            }

            auto unitInfo( const std::int16_t, const std::uint32_t info, std::string & text) -> Status override
            {
                text = info == hal::pico_5244d::kInfoVariant        ? Variant
                     : info == hal::pico_5244d::kInfoBatchAndSerial ? Serial
                                                                    : "1, 0, 3, 5";
                return 0;
            }

            auto setDeviceResolution( const std::int16_t, const std::int32_t bits) -> Status override
            {
                Calls.push_back( "resolution(" + std::to_string( bits) + ")");
                return 0;
            }

            auto maximumValue( const std::int16_t, std::int16_t & value) -> Status override { value = AdcMax; return 0; }

            auto setChannel( const std::int16_t, const std::int32_t channel, const bool enabled, const std::int32_t coupling,
                             const std::int32_t range, const float offset) -> Status override
            {
                Calls.push_back( "channel(" + std::to_string( channel) + "," + ( enabled ? "on" : "off") + ",cpl"
                                 + std::to_string( coupling) + ",range" + std::to_string( range) + ",off"
                                 + std::to_string( offset) + ")");
                return 0;
            }

            auto analogueOffsetLimits( const std::int16_t, const std::int32_t, const std::int32_t, float & maximum,
                                       float & minimum) -> Status override
            {
                maximum = OffsetLimit;
                minimum = -OffsetLimit;
                return 0;
            }

            auto setBandwidthFilter( const std::int16_t, const std::int32_t channel, const std::int32_t bandwidth) -> Status override
            {
                Calls.push_back( "bandwidth(" + std::to_string( channel) + "," + std::to_string( bandwidth) + ")");
                return 0;
            }

            auto setSimpleTrigger( const std::int16_t, const bool enable, const std::int32_t source, const std::int16_t threshold,
                                   const std::int32_t direction, const std::uint32_t delay, const std::int16_t autoMs) -> Status override
            {
                Calls.push_back( "trigger(" + std::string( enable ? "on" : "off") + ",src" + std::to_string( source)
                                 + ",adc" + std::to_string( threshold) + ",dir" + std::to_string( direction)
                                 + ",delay" + std::to_string( delay) + ",auto" + std::to_string( autoMs) + ")");
                return 0;
            }

            auto timebase( const std::int16_t, const std::uint32_t timebase, const std::int32_t, float & intervalNs,
                           std::int32_t & maxSamples) -> Status override
            {
                if( timebase < 3)
                {
                    return 14;   // PICO_INVALID_TIMEBASE
                }

                intervalNs = static_cast<float>( ( timebase - 2) * 8);
                maxSamples = MaxSamples;
                return 0;
            }

            auto setDataBuffer( const std::int16_t, const std::int32_t channel, std::int16_t * buffer,
                                const std::int32_t length) -> Status override
            {
                Buffers[ static_cast<std::size_t>( channel)] = { buffer, static_cast<std::size_t>( length) };
                Calls.push_back( "buffer(" + std::to_string( channel) + "," + std::to_string( length) + ")");
                return 0;
            }

            auto runBlock( const std::int16_t, const std::int32_t pre, const std::int32_t post, const std::uint32_t timebase,
                           std::int32_t & indisposed) -> Status override
            {
                Calls.push_back( "run(pre" + std::to_string( pre) + ",post" + std::to_string( post) + ",tb"
                                 + std::to_string( timebase) + ")");
                indisposed = 0;
                return 0;
            }

            auto isReady( const std::int16_t, bool & ready) -> Status override { ready = Ready; return 0; }
            auto stop( const std::int16_t) -> Status override { Calls.emplace_back( "stop"); return 0; }

            auto getValues( const std::int16_t, std::uint32_t & samples, std::int16_t & overflow) -> Status override
            {
                Calls.push_back( "values(" + std::to_string( samples) + ")");

                for( std::size_t channel = 0; channel < Buffers.size(); ++channel)
                {
                    auto & [ buffer, length] = Buffers[ channel];

                    for( std::size_t i = 0; buffer != nullptr && i < length && i < samples; ++i)
                    {
                        buffer[ i] = Adc( static_cast<int>( channel), i);
                    }
                }

                overflow = Overflow;
                return 0;
            }

            auto setSigGen( const std::int16_t, const std::int32_t offset, const std::uint32_t peakToPeak,
                            const std::int32_t waveType, const std::int32_t operation,
                            const double hertz) -> Status override
            {
                Calls.push_back( "siggen(type" + std::to_string( waveType) + ( operation != 0 ? "+op" + std::to_string( operation) : "")
                                 + "," + std::to_string( hertz) + "Hz,off"
                                 + std::to_string( offset) + "uV,pp" + std::to_string( peakToPeak) + "uV)");
                return SigGenStatus;
            }

            Status                     SigGenStatus{ 0 };

        private:
            std::array<std::pair<std::int16_t *, std::size_t>, 2> Buffers{};
    };

    struct Bench
    {
        std::shared_ptr<FakeScope>           Fake{ std::make_shared<FakeScope>() };
        hal::pico_5244d::Oscilloscope        Osc{ anyId(), hal::Usb{ "JO123/0456" } };

        Bench()
        {
            Osc.useApi( Fake);
        }

        // 1 ms/div (a 10 ms window, 5 us a sample, 2000 samples) and channel 1 at +/-5 V.
        void basicSetup()
        {
            Osc.configureTimebase( Osc.timebase().timePerDivision( 1_ms).config());
            Osc.configureChannel( Osc.channel<1>().range( 5_V).config());
        }
    };
} // namespace

// --- Simulated: the trace is the whole of what a channel holds ---------------

TEST( Pico5244d, SimulatedMeasuresComeFromTheTrace)
{
    hal::pico_5244d::Oscilloscope osc{ anyId(), hal::Simulated{} };

    std::vector<double> samples;

    for( int i = 0; i < 400; ++i)
    {
        samples.push_back( ( i % 100) < 50 ? 3.3 : 0.0);
    }

    osc.setSimulatedTrace( 2, core::Waveform{ core::quantityKindOf<Voltage>(),
                                              core::Waveform::Timing{ Time{ 0.0 }, Time{ 1e-6 } }, samples });

    auto ch2 = osc.channel<2>();

    EXPECT_NEAR( ch2.vpp().rawMeasure().value(),       3.3, 1e-12);
    EXPECT_NEAR( ch2.vbase().rawMeasure().value(),     0.0, 1e-12);
    EXPECT_NEAR( ch2.vtop().rawMeasure().value(),      3.3, 1e-12);
    EXPECT_NEAR( ch2.frequency().rawMeasure().value(), 10e3, 1e-6);
    EXPECT_NEAR( ch2.positiveWidth().rawMeasure().value(), 50e-6, 1e-12);
}

TEST( Pico5244d, UnmeasurableCarriesTheSpecificReason)
{
    hal::pico_5244d::Oscilloscope osc{ anyId(), hal::Simulated{} };

    osc.setSimulatedTrace( 1, core::Waveform{ core::quantityKindOf<Voltage>(),
                                              core::Waveform::Timing{ Time{ 0.0 }, Time{ 1e-6 } },
                                              std::vector<double>( 100, 1.0) });

    try
    {
        static_cast<void>( osc.channel<1>().vamplitude().rawMeasure());
        FAIL() << "a flat record has no amplitude";
    }
    catch( const core::UnmeasurableReading & reading)
    {
        EXPECT_EQ( reading.reason(), "the record is flat: no top and base to tell apart");
    }

    // ...and the extremes of that same flat record are fine.
    EXPECT_DOUBLE_EQ( osc.channel<1>().vmax().rawMeasure().value(), 1.0);

    // A channel nothing was put on has no waveform.
    EXPECT_THROW( static_cast<void>( osc.channel<2>().vpp().rawMeasure()), core::UnmeasurableReading);
}

TEST( Pico5244d, SimulatedArmAndAwait)
{
    hal::pico_5244d::Oscilloscope osc{ anyId(), hal::Simulated{} };

    EXPECT_FALSE( osc.awaitAcquisition( osc.single().config()));   // never armed

    osc.armSingle( osc.single().config());
    EXPECT_TRUE( osc.isArmed());
    EXPECT_TRUE( osc.awaitAcquisition( osc.single().config()));
    EXPECT_FALSE( osc.isArmed());

    osc.setSimulatedCaptureCompletes( false);
    osc.armSingle( osc.single().config());
    EXPECT_FALSE( osc.awaitAcquisition( osc.single().config()));
}

TEST( Pico5244d, PortsAreQualifiedByMeasurementAndRiseTimeCarriesItsThresholds)
{
    hal::pico_5244d::Oscilloscope osc{ anyId(), hal::Simulated{} };

    const auto rise = osc.channel<1>().riseTime();

    ASSERT_TRUE( rise.setup().LowThreshold && rise.setup().HighThreshold);
    EXPECT_DOUBLE_EQ( *rise.setup().LowThreshold, 0.1);
    EXPECT_DOUBLE_EQ( *rise.setup().HighThreshold, 0.9);
}

// --- Attached, through the fake library ------------------------------------

TEST( Pico5244d, OpensTheSerialAndChecksTheModel)
{
    Bench bench;

    EXPECT_EQ( bench.Osc.identity(), "Pico Technology,5244D,JO123/0456,1.0.3.5");
    EXPECT_EQ( bench.Fake->count( "open(JO123/0456,0)"), 1);

    // Once only: a second use finds the unit open.
    static_cast<void>( bench.Osc.identity());
    EXPECT_EQ( bench.Fake->count( "open("), 1);
}

TEST( Pico5244d, RefusesAnotherModelAndLetsGo)
{
    Bench bench;
    bench.Fake->Variant = "5444D";

    try
    {
        static_cast<void>( bench.Osc.identity());
        FAIL() << "a 5444D is not this driver's instrument";
    }
    catch( const hal::io::TransportError & error)
    {
        EXPECT_NE( std::string( error.what()).find( "expected a PicoScope 5244D and found \"5444D\""), std::string::npos);
    }

    EXPECT_EQ( bench.Fake->count( "close"), 1);   // the unit is not left open
}

TEST( Pico5244d, CarriesOnOnUsbPowerAlone)
{
    Bench bench;
    bench.Fake->OpenStatus = hal::pico_5244d::kPicoPowerSupplyNotConnected;

    EXPECT_NO_THROW( static_cast<void>( bench.Osc.identity()));
    EXPECT_EQ( bench.Fake->count( "power(282)"), 1);   // 0x11A, handed back to ChangePowerSource
}

TEST( Pico5244d, AUnitThatWillNotOpenIsATransportErrorNamingWhatIsAttached)
{
    Bench bench;
    bench.Fake->OpenStatus = 3;   // PICO_NOT_FOUND

    try
    {
        static_cast<void>( bench.Osc.identity());
        FAIL();
    }
    catch( const hal::io::TransportError & error)
    {
        const std::string what = error.what();

        EXPECT_NE( what.find( "PICO_NOT_FOUND"), std::string::npos) << what;
        EXPECT_NE( what.find( "JO123/0456"), std::string::npos) << what;   // the unit list
    }
}

TEST( Pico5244d, ChannelSetupPicksTheSmallestRangeAndFlipsTheOffset)
{
    Bench bench;

    // Needs +/-3 V: the +/-5 V range (index 8). A +1 V centre level is the SDK's
    // offset of -1 V, which is added to the input before digitising.
    bench.Osc.configureChannel( bench.Osc.channel<1>().range( 3_V).verticalOffset( 1_V).config());

    EXPECT_EQ( bench.Fake->last( "channel("), "channel(0,on,cpl1,range8,off-1)");

    bench.Osc.configureChannel( bench.Osc.channel<2>().range( 20_mV).coupling( hal::pico_5244d::Coupling::Ac)
                                    .bandwidth( hal::pico_5244d::Bandwidth::Limited).config());

    EXPECT_EQ( bench.Fake->last( "channel("), "channel(1,on,cpl0,range1,off0)");
    EXPECT_EQ( bench.Fake->last( "bandwidth("), "bandwidth(1,1)");
}

TEST( Pico5244d, ProbeRatioMovesTheRangeAndTheOffsetToTheInstrumentsInput)
{
    Bench bench;

    // +/-50 V at the tip of a 10:1 probe is +/-5 V at the input; a 20 V level
    // at the tip is 2 V there.
    bench.Osc.configureChannel( bench.Osc.channel<1>().probeAttenuation( 10.0).range( 50_V).verticalOffset( 20_V).config());

    EXPECT_EQ( bench.Fake->last( "channel("), "channel(0,on,cpl1,range8,off-2)");
}

TEST( Pico5244d, ARangeTheInputCannotTakeIsRefusedNotClamped)
{
    Bench bench;

    EXPECT_THROW( bench.Osc.configureChannel( bench.Osc.channel<1>().range( 50_V).config()),
                  hal::pico_5244d::SettingOutOfRange);

    // With a 10:1 probe the same 50 V is fine.
    EXPECT_NO_THROW( bench.Osc.configureChannel( bench.Osc.channel<1>().probeAttenuation( 10.0).range( 50_V).config()));
}

TEST( Pico5244d, AnOffsetTheRangeDoesNotAllowIsRefusedByName)
{
    Bench bench;
    bench.Fake->OffsetLimit = 0.25F;

    EXPECT_THROW( bench.Osc.configureChannel( bench.Osc.channel<1>().range( 5_V).verticalOffset( 3_V).config()),
                  hal::pico_5244d::SettingOutOfRange);
}

TEST( Pico5244d, ResolutionIsSetAndTheChannelsAreAskedAgain)
{
    Bench bench;
    bench.Osc.configureChannel( bench.Osc.channel<1>().range( 5_V).config());

    const auto before = bench.Fake->count( "channel(");

    bench.Osc.configureAcquisition( bench.Osc.acquisition().resolution( hal::pico_5244d::Resolution::Bits12).config());

    EXPECT_EQ( bench.Fake->last( "resolution("), "resolution(1)");
    EXPECT_GT( bench.Fake->count( "channel("), before);
}

TEST( Pico5244d, ASingleShotRunsATriggeredBlockWithTheTimebaseFoundBySearch)
{
    Bench bench;
    bench.basicSetup();
    bench.Osc.configureTrigger( bench.Osc.trigger().edgeSource<1>().level( 1_V)
                                    .slope( hal::pico_5244d::TriggerSlope::Falling).config());

    bench.Osc.armSingle( bench.Osc.single().config());

    // 10 ms over 2000 points is 5 us, which is (n-2)*8 ns for n = 627; the
    // trigger is at the window's centre by default, so half before it.
    EXPECT_EQ( bench.Fake->last( "run("), "run(pre1000,post1000,tb627)");

    // 1 V on a +/-5 V range is a fifth of the ADC's 32512 codes.
    EXPECT_EQ( bench.Fake->last( "trigger("), "trigger(on,src0,adc6502,dir3,delay0,auto1000)");
    EXPECT_TRUE( bench.Osc.isArmed());
}

TEST( Pico5244d, ReferenceAndPositionPlaceTheTrigger)
{
    Bench bench;
    bench.basicSetup();

    // Left reference, position 0: the record starts at the trigger.
    bench.Osc.configureTimebase( bench.Osc.timebase().reference( hal::pico_5244d::TimebaseReference::Left).config());
    bench.Osc.armSingle( bench.Osc.single().config());
    EXPECT_EQ( bench.Fake->last( "run("), "run(pre0,post2000,tb627)");

    // Left, position 2 ms: the reference point is 2 ms after the trigger, so the
    // window starts then -- the SDK's delay, 400 samples of 5 us.
    bench.Osc.configureTimebase( bench.Osc.timebase().position( 2_ms).config());
    bench.Osc.armSingle( bench.Osc.single().config());
    EXPECT_EQ( bench.Fake->last( "run("), "run(pre0,post2000,tb627)");
    EXPECT_EQ( bench.Fake->last( "trigger("), "trigger(on,src0,adc0,dir2,delay400,auto1000)");

    // Right reference: the whole window is before the trigger.
    bench.Osc.configureTimebase( bench.Osc.timebase().reference( hal::pico_5244d::TimebaseReference::Right).position( 0_s).config());
    bench.Osc.armSingle( bench.Osc.single().config());
    EXPECT_EQ( bench.Fake->last( "run("), "run(pre1999,post1,tb627)");
}

TEST( Pico5244d, NormalSweepWaitsForEver)
{
    Bench bench;
    bench.basicSetup();
    bench.Osc.configureTrigger( bench.Osc.trigger().sweep( hal::pico_5244d::TriggerSweep::Normal).config());

    bench.Osc.armSingle( bench.Osc.single().config());

    EXPECT_NE( bench.Fake->last( "trigger(").find( ",auto0)"), std::string::npos);
}

TEST( Pico5244d, AWindowTheMemoryCannotHoldIsRefusedBeforeItRuns)
{
    Bench bench;
    bench.basicSetup();
    bench.Fake->MaxSamples = 1000;

    EXPECT_THROW( bench.Osc.armSingle( bench.Osc.single().config()), hal::pico_5244d::SettingOutOfRange);
    EXPECT_EQ( bench.Fake->count( "run("), 0);
}

TEST( Pico5244d, ArmingNeedsATimebaseAndAChannel)
{
    Bench bench;

    EXPECT_THROW( bench.Osc.armSingle( bench.Osc.single().config()), hal::pico_5244d::NotSetUp);   // no timebase

    bench.Osc.configureTimebase( bench.Osc.timebase().timePerDivision( 1_ms).config());

    EXPECT_THROW( bench.Osc.armSingle( bench.Osc.single().config()), hal::pico_5244d::NotSetUp);   // no channel

    bench.Osc.configureChannel( bench.Osc.channel<1>().range( 5_V).config());
    bench.Osc.configureTrigger( bench.Osc.trigger().edgeSource<2>().config());

    EXPECT_THROW( bench.Osc.armSingle( bench.Osc.single().config()), hal::pico_5244d::NotSetUp);   // trigger on an unset channel
}

TEST( Pico5244d, ArmRefusesAPreTriggerLongerThanItsTimeout)
{
    Bench bench;
    bench.Osc.configureTimebase( bench.Osc.timebase().timePerDivision( 1_s).config());   // 10 s, half before the trigger
    bench.Osc.configureChannel( bench.Osc.channel<1>().range( 5_V).config());

    EXPECT_THROW( bench.Osc.armSingle( bench.Osc.single().config()), hal::io::TransportError);
    EXPECT_GE( bench.Fake->count( "stop"), 1);
}

TEST( Pico5244d, AwaitConvertsCountsToVoltsAtTheProbeTip)
{
    Bench bench;
    bench.basicSetup();

    // Half of full scale on a +/-5 V range, and a +1 V centre level (SDK offset -1).
    bench.Osc.configureChannel( bench.Osc.channel<1>().verticalOffset( 1_V).config());
    bench.Fake->Adc = []( int, std::size_t) { return std::int16_t{ 16256 }; };

    bench.Osc.armSingle( bench.Osc.single().config());
    ASSERT_TRUE( bench.Osc.awaitAcquisition( bench.Osc.single().config()));

    const auto trace = bench.Osc.fetchWaveform( bench.Osc.channel<1>().waveform().config());

    ASSERT_EQ( trace.size(), 2000U);
    EXPECT_NEAR( trace.at( 0), 3.5, 1e-9);                       // 2.5 V seen + 1 V of offset
    EXPECT_NEAR( trace.timing().Increment.value(), 5e-6, 1e-12);
    EXPECT_NEAR( trace.timing().Origin.value(), -5e-3, 1e-12);   // a thousand samples before the trigger
}

TEST( Pico5244d, AMeasureAfterAwaitReadsTheHeldCaptureWithoutRunningAnother)
{
    Bench bench;
    bench.basicSetup();
    bench.Fake->Adc = []( int, std::size_t i) { return static_cast<std::int16_t>( ( i / 100) % 2 == 0 ? 20000 : 0); };

    bench.Osc.armSingle( bench.Osc.single().config());
    ASSERT_TRUE( bench.Osc.awaitAcquisition( bench.Osc.single().config()));

    const int runs = bench.Fake->count( "run(");

    EXPECT_NEAR( bench.Osc.channel<1>().vtop().rawMeasure().value(),  20000.0 / 32512.0 * 5.0, 1e-9);
    EXPECT_NEAR( bench.Osc.channel<1>().vbase().rawMeasure().value(), 0.0, 1e-9);
    EXPECT_NEAR( bench.Osc.channel<1>().frequency().rawMeasure().value(), 1.0 / ( 200 * 5e-6), 1e-3);

    EXPECT_EQ( bench.Fake->count( "run("), runs);
}

TEST( Pico5244d, AMeasureWithNothingHeldTakesAFreeRunningCaptureEachTime)
{
    Bench bench;
    bench.basicSetup();
    bench.Fake->Adc = []( int, std::size_t) { return std::int16_t{ 8128 }; };

    EXPECT_NEAR( bench.Osc.channel<1>().vmax().rawMeasure().value(), 1.25, 1e-9);
    EXPECT_NEAR( bench.Osc.channel<1>().vmin().rawMeasure().value(), 1.25, 1e-9);

    EXPECT_EQ( bench.Fake->count( "run("), 2);
    EXPECT_EQ( bench.Fake->last( "trigger("), "trigger(off,src0,adc0,dir2,delay0,auto0)");
}

TEST( Pico5244d, ASetupDropsTheHeldCapture)
{
    Bench bench;
    bench.basicSetup();

    bench.Osc.armSingle( bench.Osc.single().config());
    ASSERT_TRUE( bench.Osc.awaitAcquisition( bench.Osc.single().config()));

    bench.Osc.configureChannel( bench.Osc.channel<1>().range( 10_V).config());

    const int runs = bench.Fake->count( "run(");

    static_cast<void>( bench.Osc.channel<1>().vmax().rawMeasure());

    EXPECT_EQ( bench.Fake->count( "run("), runs + 1);   // measured afresh, at the new range
}

TEST( Pico5244d, AClippedRecordIsNotMeasured)
{
    Bench bench;
    bench.basicSetup();
    bench.Fake->Adc      = []( int, std::size_t i) { return static_cast<std::int16_t>( i % 2 == 0 ? 32512 : -32512); };
    bench.Fake->Overflow = 0b01;   // channel A

    try
    {
        static_cast<void>( bench.Osc.channel<1>().vmax().rawMeasure());
        FAIL() << "a clipped record's maximum is the range, not the signal";
    }
    catch( const core::UnmeasurableReading & reading)
    {
        EXPECT_NE( reading.reason().find( "clipped"), std::string_view::npos);
    }
}

TEST( Pico5244d, MeasuringAChannelNeverSetUpIsAFaultNotAReading)
{
    Bench bench;
    bench.basicSetup();

    EXPECT_THROW( static_cast<void>( bench.Osc.channel<2>().vpp().rawMeasure()), hal::pico_5244d::NotSetUp);
}

TEST( Pico5244d, AwaitTimingOutIsAResultAndStopsTheUnit)
{
    Bench bench;
    bench.basicSetup();
    bench.Fake->Ready = false;

    bench.Osc.armSingle( bench.Osc.single().config());

    const int stops = bench.Fake->count( "stop");

    EXPECT_FALSE( bench.Osc.awaitAcquisition( bench.Osc.single().timeout( 20_ms).config()));
    EXPECT_GT( bench.Fake->count( "stop"), stops);
    EXPECT_FALSE( bench.Osc.lastAcquisitionCompleted());

    // And nothing is held to be measured.
    bench.Fake->Ready = true;
    const int runs = bench.Fake->count( "run(");
    static_cast<void>( bench.Osc.channel<1>().vmax().rawMeasure());
    EXPECT_EQ( bench.Fake->count( "run("), runs + 1);
}

TEST( Pico5244d, SafeStopsAnOpenUnitAndNeverOpensOne)
{
    Bench bench;

    bench.Osc.safe();

    EXPECT_TRUE( bench.Fake->Calls.empty());   // nothing opened to stop

    bench.basicSetup();
    bench.Osc.armSingle( bench.Osc.single().config());

    const int stops = bench.Fake->count( "stop");

    bench.Osc.safe();

    EXPECT_GT( bench.Fake->count( "stop"), stops);
    EXPECT_FALSE( bench.Osc.isArmed());
    EXPECT_FALSE( bench.Osc.awaitAcquisition( bench.Osc.single().config()));   // the armed capture is forgotten
}

TEST( Pico5244d, CloseSessionReleasesTheUnitAndTheNextUseReappliesTheSetup)
{
    Bench bench;
    bench.basicSetup();
    static_cast<void>( bench.Osc.identity());

    bench.Osc.closeSession();

    EXPECT_EQ( bench.Fake->count( "close"), 1);

    const int channels = bench.Fake->count( "channel(");

    static_cast<void>( bench.Osc.identity());

    EXPECT_EQ( bench.Fake->count( "open("), 2);
    EXPECT_EQ( bench.Fake->count( "channel("), channels + 1);   // channel 1 again; channel 2 was never set up
}

TEST( Pico5244d, DescribeConfigReadsLikeTheOtherScopes)
{
    hal::pico_5244d::Oscilloscope osc{ anyId(), hal::Simulated{} };

    const auto description = describeConfig( osc.channel<2>().range( 5_V).coupling( hal::pico_5244d::Coupling::Ac)
                                                 .probeAttenuation( 10.0).config());

    EXPECT_NE( description.Settings.find( "ch2.coupling=Ac"), std::string::npos) << description.Settings;
    EXPECT_NE( description.Settings.find( "ch2.probe=10x"), std::string::npos) << description.Settings;
}

TEST( Pico5244d, NoLibraryIsAnUnsupportedTransportSayingWhatWasTried)
{
    //
    // On a machine with no PicoSDK -- every machine this is developed on --
    // the library cannot load, and the failure has to say where it looked and
    // how to point at it. Skipped where a library genuinely is installed.
    //
    if( !hal::pico_5244d::ps5000aLibraryPath().empty())
    {
        GTEST_SKIP() << "a ps5000a library is installed here: " << hal::pico_5244d::ps5000aLibraryPath();
    }

    try
    {
        static_cast<void>( hal::pico_5244d::loadPs5000a());
        FAIL();
    }
    catch( const hal::io::UnsupportedTransport & error)
    {
        EXPECT_NE( std::string( error.what()).find( "THORIUM_PS5000A_LIBRARY"), std::string::npos);
    }

    // An attached driver asked for a reading then says the same thing, rather than crashing.
    hal::pico_5244d::Oscilloscope osc{ anyId(), hal::Usb{ "JO123/0456" } };

    EXPECT_THROW( static_cast<void>( osc.identity()), hal::io::UnsupportedTransport);
}

TEST( Pico5244d, StatusesAreDescribed)
{
    EXPECT_NE( hal::pico_5244d::describeStatus( 3).find( "PICO_NOT_FOUND"), std::string::npos);
    EXPECT_EQ( hal::pico_5244d::describeStatus( 0x12345678), "0x12345678");
}


// --- The generator face --------------------------------------------------------

namespace
{
    //
    // Both faces of one box on one address, over one fake library -- the
    // arrangement a rig's two rows on Pico1 make.
    //
    struct TwoFaceBench
    {
        std::shared_ptr<FakeScope>               Fake{ std::make_shared<FakeScope>() };
        hal::pico_5244d::Oscilloscope            Osc{ anyId(), hal::Usb{ "JO123/0456" } };
        hal::pico_5244d::WaveformGenerator       Gen{ anyId(), hal::Usb{ "JO123/0456" } };

        TwoFaceBench()
        {
            Osc.useApi( Fake);
        }
    };
} // namespace

TEST( Pico5244dGenerator, ASineIsOneCallWithTheShapeFrequencyAndMicrovolts)
{
    TwoFaceBench bench;

    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 2_V).offset( 0.5_V).config());

    EXPECT_EQ( bench.Fake->last( "siggen("), "siggen(type0,1000Hz,off500000uV,pp2000000uV)");
    EXPECT_TRUE( bench.Gen.isEnabled());
    EXPECT_EQ( bench.Gen.function(), "sine");
}

TEST( Pico5244dGenerator, EachShapeIsItsOwnWaveType)
{
    TwoFaceBench bench;

    const auto type = [ & ]( const std::string & call) { return call.substr( 0, call.find( ',')); };

    applyDriver( bench.Gen.square().frequency( 1_kHz).amplitude( 1_V).config());
    EXPECT_EQ( type( bench.Fake->last( "siggen(")), "siggen(type1");

    applyDriver( bench.Gen.triangle().frequency( 1_kHz).amplitude( 1_V).config());
    EXPECT_EQ( type( bench.Fake->last( "siggen(")), "siggen(type2");

    applyDriver( bench.Gen.rampUp().frequency( 1_kHz).amplitude( 1_V).config());
    EXPECT_EQ( type( bench.Fake->last( "siggen(")), "siggen(type3");

    applyDriver( bench.Gen.rampDown().frequency( 1_kHz).amplitude( 1_V).config());
    EXPECT_EQ( type( bench.Fake->last( "siggen(")), "siggen(type4");

    applyDriver( bench.Gen.noise().amplitude( 1_V).config());
    EXPECT_EQ( type( bench.Fake->last( "siggen(")), "siggen(type0+op1");
}

// The 5000D plays triangle and ramps at half the frequency it is given.
TEST( Pico5244dGenerator, TriangleAndRampsAskTwiceTheFrequency)
{
    TwoFaceBench bench;

    applyDriver( bench.Gen.triangle().frequency( 1_kHz).amplitude( 1_V).config());
    EXPECT_NE( bench.Fake->last( "siggen(").find( ",2000Hz,"), std::string::npos);

    applyDriver( bench.Gen.rampUp().frequency( 1_kHz).amplitude( 1_V).config());
    EXPECT_NE( bench.Fake->last( "siggen(").find( ",2000Hz,"), std::string::npos);

    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config());
    EXPECT_NE( bench.Fake->last( "siggen(").find( ",1000Hz,"), std::string::npos);
}

TEST( Pico5244dGenerator, DcIsTheOffsetAndNothingElse)
{
    TwoFaceBench bench;

    applyDriver( bench.Gen.dc().offset( 1.5_V).config());

    EXPECT_EQ( bench.Fake->last( "siggen("), "siggen(type8,0Hz,off1500000uV,pp0uV)");
}

//
// The point of the two-face arrangement: one open, however many faces talk and
// in whichever order. A second OpenUnit would be refused by the SDK.
//
TEST( Pico5244dGenerator, BothFacesShareTheOneOpenUnit)
{
    TwoFaceBench bench;

    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config());
    bench.Osc.configureChannel( bench.Osc.channel<1>().range( 2_V).config());
    static_cast<void>( bench.Osc.identity());
    static_cast<void>( bench.Gen.identity());

    EXPECT_EQ( bench.Fake->count( "open("), 1);
}

TEST( Pico5244dGenerator, TheScopeSetUpAfterTheGeneratorOpenedTheUnitStillGetsItsResolution)
{
    TwoFaceBench bench;

    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config());

    // The generator opened the unit without an opinion on resolution.
    EXPECT_EQ( bench.Fake->last( "open("), "open(JO123/0456,0)");

    bench.Osc.configureAcquisition( bench.Osc.acquisition().resolution( hal::pico_5244d::Resolution::Bits12).config());

    EXPECT_EQ( bench.Fake->last( "resolution("), "resolution(1)");
    EXPECT_EQ( bench.Fake->count( "open("), 1);
}

TEST( Pico5244dGenerator, TheScopesSetupIsAppliedToAUnitTheGeneratorOpened)
{
    TwoFaceBench bench;

    // Recorded detached from the unit: a simulated face would not have reached it.
    bench.Osc.configureChannel( bench.Osc.channel<1>().range( 5_V).config());

    const int before = bench.Fake->count( "channel(");

    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config());
    static_cast<void>( bench.Osc.identity());

    EXPECT_EQ( bench.Fake->count( "open("), 1);
    EXPECT_EQ( bench.Fake->count( "channel("), before);   // already pushed when the scope opened it
}

TEST( Pico5244dGenerator, ClosingFromEitherFaceReleasesTheUnitForBoth)
{
    TwoFaceBench bench;

    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config());
    bench.Osc.configureChannel( bench.Osc.channel<1>().range( 2_V).config());

    bench.Osc.closeSession();

    EXPECT_EQ( bench.Fake->count( "close"), 1);

    // The next use by the other face opens it again.
    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config());

    EXPECT_EQ( bench.Fake->count( "open("), 2);
}

TEST( Pico5244dGenerator, RemoveTurnsTheOutputOffAsZeroVolts)
{
    TwoFaceBench bench;

    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 2_V).config());
    removeDriver( bench.Gen.sine().config());

    EXPECT_EQ( bench.Fake->last( "siggen("), "siggen(type8,0Hz,off0uV,pp0uV)");
    EXPECT_FALSE( bench.Gen.isEnabled());
    EXPECT_FALSE( isEnergised( bench.Gen.sine().config()));
}

TEST( Pico5244dGenerator, ALaterApplyKeepsWhatAnEarlierOneNamed)
{
    TwoFaceBench bench;

    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 2_V).offset( 0.25_V).config());
    applyDriver( bench.Gen.square().frequency( 5_kHz).config());

    // The SDK sets everything at once, so amplitude and offset are carried over.
    EXPECT_EQ( bench.Fake->last( "siggen("), "siggen(type1,5000Hz,off250000uV,pp2000000uV)");
}

TEST( Pico5244dGenerator, WhatWasNeverSetAndHasNoZeroIsRefusedNotGuessed)
{
    TwoFaceBench bench;

    EXPECT_THROW( applyDriver( bench.Gen.sine().amplitude( 1_V).config()), hal::pico_5244d::SettingOutOfRange);
    EXPECT_THROW( applyDriver( bench.Gen.sine().frequency( 1_kHz).config()), hal::pico_5244d::SettingOutOfRange);

    EXPECT_EQ( bench.Fake->count( "siggen("), 0);
}

//
// The SDK clips a waveform that leaves the output range and answers PICO_OK:
// the one failure a script cannot see from the status, so it is refused here.
//
TEST( Pico5244dGenerator, ASettingThatWouldBeClippedIsRefusedBeforeAnythingIsSent)
{
    TwoFaceBench bench;

    // 4 Vpp about +1 V peaks at +3 V; the output reaches +2 V.
    EXPECT_THROW( applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 4_V).offset( 1_V).config()),
                  hal::pico_5244d::SettingOutOfRange);

    // 2 Vpp about +1 V peaks at +2 V exactly: allowed.
    EXPECT_NO_THROW( applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 2_V).offset( 1_V).config()));

    EXPECT_THROW( applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 5_V).config()), hal::pico_5244d::SettingOutOfRange);
    EXPECT_THROW( applyDriver( bench.Gen.dc().offset( 2.5_V).config()), hal::pico_5244d::SettingOutOfRange);
    EXPECT_THROW( applyDriver( bench.Gen.sine().frequency( 30_MHz).amplitude( 1_V).config()), hal::pico_5244d::SettingOutOfRange);
    EXPECT_THROW( applyDriver( bench.Gen.sine().frequency( 0.001_Hz).amplitude( 1_V).config()), hal::pico_5244d::SettingOutOfRange);

    EXPECT_EQ( bench.Fake->count( "siggen("), 1);
}

TEST( Pico5244dGenerator, AnSdkRefusalIsAFaultNamingTheCall)
{
    TwoFaceBench bench;
    bench.Fake->SigGenStatus = 0x7D;

    try
    {
        applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config());
        FAIL() << "the SDK said no";
    }
    catch( const hal::pico_5244d::PicoFault & fault)
    {
        EXPECT_NE( std::string( fault.what()).find( "SetSigGenBuiltInV2"), std::string::npos) << fault.what();
    }
}

TEST( Pico5244dGenerator, SafeSilencesAnOpenUnitAndNeverOpensOne)
{
    TwoFaceBench bench;

    bench.Gen.safe();

    EXPECT_TRUE( bench.Fake->Calls.empty());

    // Opened by the *scope*: the generator's safing finds it all the same.
    bench.Osc.configureChannel( bench.Osc.channel<1>().range( 2_V).config());
    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config());

    bench.Gen.safe();

    EXPECT_EQ( bench.Fake->last( "siggen("), "siggen(type8,0Hz,off0uV,pp0uV)");
    EXPECT_FALSE( bench.Gen.isEnabled());
    EXPECT_FALSE( bench.Gen.frequency().has_value());
}

TEST( Pico5244dGenerator, SafeNeverRaisesOnAUnitThatHasGone)
{
    TwoFaceBench bench;

    applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config());
    bench.Fake->SigGenStatus = 0x7D;

    EXPECT_NO_THROW( bench.Gen.safe());
}

TEST( Pico5244dGenerator, SimulatedRecordsTheSettingsAndReachesNothing)
{
    hal::pico_5244d::WaveformGenerator gen{ anyId(), hal::Simulated{} };

    applyDriver( gen.sine().frequency( 2_kHz).amplitude( 1_V).offset( 0.1_V).config());

    EXPECT_TRUE( gen.isSimulated());
    EXPECT_TRUE( gen.outputIsOn());
    EXPECT_EQ( gen.frequency()->value(), 2000.0);
    EXPECT_NEAR( gen.amplitude()->value(), 1.0, 1e-12);
    EXPECT_NEAR( gen.offset()->value(), 0.1, 1e-12);

    removeDriver( gen.sine().config());

    EXPECT_FALSE( gen.outputIsOn());
}

TEST( Pico5244dGenerator, TheJournalNamesTheShapeFirstAndOnlyWhatWasSet)
{
    TwoFaceBench bench;

    const auto description = describeConfig( bench.Gen.square().frequency( 1_kHz).amplitude( 2_V).config());

    EXPECT_EQ( description.Settings.rfind( "square", 0), 0u) << description.Settings;
    EXPECT_NE( description.Settings.find( "frequency="), std::string::npos) << description.Settings;
    EXPECT_EQ( description.Settings.find( "offset="), std::string::npos) << description.Settings;
}

TEST( Pico5244dGenerator, AGeneratorOnTheWrongModelIsRefusedAndTheUnitClosed)
{
    TwoFaceBench bench;
    bench.Fake->Variant = "5244D MSO";

    EXPECT_THROW( applyDriver( bench.Gen.sine().frequency( 1_kHz).amplitude( 1_V).config()), hal::io::TransportError);
    EXPECT_EQ( bench.Fake->count( "close"), 1);
    EXPECT_EQ( bench.Fake->count( "siggen("), 0);
}
