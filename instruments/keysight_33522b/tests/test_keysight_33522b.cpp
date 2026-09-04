//
// hal::keysight_33522b::Wfg33522B's own tests.
//
// Two halves, the same split every driver package in this tree has. The first
// tests the simulated driver -- what a script sees, what it is refused, and
// what a run journal is told. The second tests the wire: the exact commands
// this driver sends and, more than for any other driver here, the *order* it
// sends them in, because on this instrument the order is the whole meaning
// (see src/keysight_33522b.cpp's program(), where each step is justified by
// what the step after it would otherwise mean).
//
// Suite names Wfg33522B / Wfg33522BLimits / Wfg33522BWire, following the
// newest driver packages (Edu36311A / Edu36311ARating / Edu36311AWire) rather
// than the historic HalInstrument suite the older drivers still use.
//
#include "hal/keysight_33522b.hpp"
#include "hal/verbs/source.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <memory>
#include <string>
#include <vector>

#include "core/meta.hpp"

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape rig/tests/test_safing.cpp uses
// for hal::SafeableInstrument, and hal/tests/driver/test_address.cpp for the
// hal::ReachableOver mechanism itself).
//
// Three connectors, and the interesting one is hal::Gpib: it is accepted even
// though a 33522B does not necessarily have it, because a GPIB module is a
// factory- or user-installable option on this series. That is the difference
// between this driver and hal::keysight_dsox1202g::DSOX1202G, which rejects
// hal::Lan outright -- no option adds a network connector to a 1000 X-Series.
// A constraint here is a claim about what the model *can* have; whether one
// particular box does is a question for openTransport.
//
// hal::Serial is the rejection that matters: this instrument has no RS-232
// port at all, so a rig row addressing it down one fails to compile rather
// than failing to open.
//
namespace
{
    static_assert(   std::constructible_from< hal::keysight_33522b::Wfg33522B, hal::InstrumentId, hal::Lan> );
    static_assert(   std::constructible_from< hal::keysight_33522b::Wfg33522B, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< hal::keysight_33522b::Wfg33522B, hal::InstrumentId, hal::Gpib> );
    static_assert(   std::constructible_from< hal::keysight_33522b::Wfg33522B, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::keysight_33522b::Wfg33522B, hal::InstrumentId, hal::Serial> );
    static_assert( ! std::constructible_from< hal::keysight_33522b::Wfg33522B, hal::InstrumentId> );
} // namespace

using namespace core::literals;
using namespace core::quantities;

using hal::keysight_33522b::SettingOutOfRange;
using hal::keysight_33522b::Termination;
using hal::keysight_33522b::Wfg33522B;

namespace
{
    //
    // ValidChannel's compile-time bound, checked the concept-wrapped way -- see
    // core/tests/criteria/test_static_constraints.cpp's own comment for why a
    // bare static_assert(!requires{...}) is unreliable and this
    // routing-through-a-concept form isn't.
    //
    // channel<1>() on a single-channel member of this series is the case this
    // bound is really for, and it is the one this driver cannot catch: a
    // 33521B has one output, and a rig re-pointed at one gets its refusal from
    // the identity check on the wire instead (see the AnotherMemberOfTheSeries
    // tests below). What the bound catches is the arithmetic mistake --
    // channel<3>() on a two-channel box.
    //
    template<unsigned N>
    concept CanChannel = requires( Wfg33522B & wfg) { wfg.template channel<N>(); };

    static_assert(  CanChannel<1> );
    static_assert(  CanChannel<2> );
    static_assert( !CanChannel<0> );
    static_assert( !CanChannel<3> );

    //
    // Which settings each shape has, which is the whole reason the shapes are
    // tags rather than an enum. Every one of these is a compile error a script
    // gets instead of a setting the instrument would silently ignore.
    //
    template<typename BuilderT>
    concept CanDutyCycle = requires( const BuilderT & b) { b.dutyCycle( 50.0); };

    template<typename BuilderT>
    concept CanSymmetry = requires( const BuilderT & b) { b.symmetry( 50.0); };

    template<typename BuilderT>
    concept CanFrequency = requires( const BuilderT & b) { b.frequency( 1.0_kHz); };

    template<typename BuilderT>
    concept CanAmplitude = requires( const BuilderT & b) { b.amplitude( 1.0_V); };

    using SineB     = hal::keysight_33522b::WaveformBuilder<hal::keysight_33522b::Sine>;
    using SquareB   = hal::keysight_33522b::WaveformBuilder<hal::keysight_33522b::Square>;
    using RampB     = hal::keysight_33522b::WaveformBuilder<hal::keysight_33522b::Ramp>;
    using TriangleB = hal::keysight_33522b::WaveformBuilder<hal::keysight_33522b::Triangle>;
    using PulseB    = hal::keysight_33522b::WaveformBuilder<hal::keysight_33522b::Pulse>;
    using NoiseB    = hal::keysight_33522b::WaveformBuilder<hal::keysight_33522b::Noise>;
    using DcB       = hal::keysight_33522b::WaveformBuilder<hal::keysight_33522b::Dc>;

    // Duty cycle: square and pulse, and nothing else.
    static_assert(  CanDutyCycle<SquareB> );
    static_assert(  CanDutyCycle<PulseB> );
    static_assert( !CanDutyCycle<SineB> );
    static_assert( !CanDutyCycle<RampB> );
    static_assert( !CanDutyCycle<TriangleB> );

    // Symmetry: the ramp alone. A triangle IS a symmetric ramp on this
    // instrument, and has its own FUNCtion keyword rather than a symmetry.
    static_assert(  CanSymmetry<RampB> );
    static_assert( !CanSymmetry<TriangleB> );
    static_assert( !CanSymmetry<SquareB> );

    // Frequency: everything periodic. Noise and DC are not.
    static_assert(  CanFrequency<SineB> );
    static_assert(  CanFrequency<PulseB> );
    static_assert( !CanFrequency<NoiseB> );
    static_assert( !CanFrequency<DcB> );

    // Amplitude: everything but DC, whose level is its offset.
    static_assert(  CanAmplitude<SineB> );
    static_assert(  CanAmplitude<NoiseB> );
    static_assert( !CanAmplitude<DcB> );

    // And the offset, which every shape has -- including DC, where it is the
    // only setting there is.
    static_assert( requires( const DcB & b) { b.offset( 1.0_V); } );

    //
    // Whatever the linking deployment's first instrument is called. A driver's
    // tests have no business knowing that this repo's bench rig calls this
    // generator Wfg1 -- see the top-level CMakeLists.txt on the packageability
    // defect that hard-coded ids in a driver's tests cause.
    //
    [[nodiscard]]
    auto anyId() -> hal::InstrumentId
    {
        return core::meta::values<hal::InstrumentId>[ 0];
    }

    //
    // The sourcing verbs, as engine objects rather than as hal's Apply and
    // Remove globals: those are defined in hal_rig (see
    // framework/hal/CMakeLists.txt), which a driver's test target deliberately
    // does not link -- that link boundary is what stops a test here quietly
    // becoming a rig test. Constructing an engine is the same call the global
    // is; see hal/verbs/source.hpp and core/verbs/source.hpp.
    //
    // So every test below goes through the real verb -- the ADL dispatch to
    // applyDriver/removeDriver included -- rather than calling the driver's
    // members behind the verb's back.
    //
    template<typename BuilderT>
    auto applied( const BuilderT & chain) -> void
    {
        ApplyEngine{}( chain);
    }

    template<typename BuilderT>
    auto removed( const BuilderT & chain) -> void
    {
        RemoveEngine{}( chain);
    }
} // namespace

// ===========================================================================
// The simulated driver
// ===========================================================================

TEST( Wfg33522B, AppliedSettingsReadBackPerChannel)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    applied( wfg.channel<1>().sine().frequency( 1.0_kHz).amplitude( 2.0_V));

    EXPECT_EQ( wfg.function( 1), "sine");
    ASSERT_TRUE( wfg.frequency( 1).has_value());
    EXPECT_DOUBLE_EQ( wfg.frequency( 1)->value(), 1000.0);
    ASSERT_TRUE( wfg.amplitude( 1).has_value());
    EXPECT_DOUBLE_EQ( wfg.amplitude( 1)->value(), 2.0);

    EXPECT_TRUE(  wfg.isEnabled( 1));
    EXPECT_FALSE( wfg.isEnabled( 2));
}

//
// The two connectors are independent, which is the property that makes one
// InstrumentId over both of them honest -- see the header's comparison with
// the EDU36311A, whose three outputs are three instruments precisely because
// they are not interchangeable this way.
//
TEST( Wfg33522B, TheTwoChannelsDoNotDisturbEachOther)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    applied( wfg.channel<1>().sine().frequency( 1.0_kHz).amplitude( 2.0_V));
    applied( wfg.channel<2>().square().frequency( 10.0_kHz).amplitude( 3.3_V).dutyCycle( 25.0));

    EXPECT_EQ( wfg.function( 1), "sine");
    EXPECT_EQ( wfg.function( 2), "square");

    EXPECT_DOUBLE_EQ( wfg.frequency( 1)->value(),  1000.0);
    EXPECT_DOUBLE_EQ( wfg.frequency( 2)->value(), 10000.0);

    removed( wfg.channel<1>().sine());

    EXPECT_FALSE( wfg.isEnabled( 1));
    EXPECT_TRUE(  wfg.isEnabled( 2));
}

//
// The no-argument isEnabled() the electrical interlock asks -- "either
// output", not "channel one". A generator with one connector live is live.
//
TEST( Wfg33522B, TheInstrumentWideIsEnabledIsTrueIfEitherOutputIs)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    EXPECT_FALSE( wfg.isEnabled());

    applied( wfg.channel<2>().sine().amplitude( 1.0_V));

    EXPECT_TRUE( wfg.isEnabled());

    removed( wfg.channel<2>().sine());

    EXPECT_FALSE( wfg.isEnabled());
}

TEST( Wfg33522B, TheNoArgumentRemoveOutputTakesBothChannelsDown)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    applied( wfg.channel<1>().sine().amplitude( 1.0_V));
    applied( wfg.channel<2>().sine().amplitude( 1.0_V));

    wfg.removeOutput();

    EXPECT_FALSE( wfg.isEnabled( 1));
    EXPECT_FALSE( wfg.isEnabled( 2));
}

//
// The instrument powers up expecting 50 Ohm, so that is what this driver
// believes until an Apply says otherwise -- and it has to believe *something*,
// because the amplitude and offset limits are quoted against it.
//
TEST( Wfg33522B, TheTerminationDefaultsToFiftyOhmAndIsRememberedOnceNamed)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    EXPECT_EQ( wfg.termination( 1), Termination::Ohms50);

    applied( wfg.channel<1>().sine().amplitude( 1.0_V).into( Termination::HighImpedance));

    EXPECT_EQ( wfg.termination( 1), Termination::HighImpedance);

    // An Apply that says nothing about it leaves it where it was.
    applied( wfg.channel<1>().sine().amplitude( 2.0_V));

    EXPECT_EQ( wfg.termination( 1), Termination::HighImpedance);
}

//
// safe() and removed() differ in exactly one way, and it is the way that
// matters after an unattended re-enable: Remove leaves the signal programmed,
// safe collapses it.
//
TEST( Wfg33522B, SafeCollapsesTheSignalWhereRemoveLeavesIt)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    applied( wfg.channel<1>().sine().frequency( 1.0_kHz).amplitude( 5.0_V).offset( 1.0_V));

    removed( wfg.channel<1>().sine());

    EXPECT_FALSE( wfg.isEnabled( 1));
    ASSERT_TRUE(  wfg.amplitude( 1).has_value());
    EXPECT_DOUBLE_EQ( wfg.amplitude( 1)->value(), 5.0);

    applied( wfg.channel<1>().sine().frequency( 1.0_kHz).amplitude( 5.0_V).offset( 1.0_V));

    wfg.safe();

    EXPECT_FALSE( wfg.isEnabled( 1));
    EXPECT_FALSE( wfg.amplitude( 1).has_value());
    EXPECT_FALSE( wfg.offset( 1).has_value());
}

TEST( Wfg33522B, SafeTakesBothChannelsDown)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    applied( wfg.channel<1>().sine().amplitude( 1.0_V));
    applied( wfg.channel<2>().ramp().amplitude( 1.0_V).symmetry( 50.0));

    wfg.safe();

    EXPECT_FALSE( wfg.isEnabled());
}

//
// This driver satisfies the two compile-time contracts hal reflects over: it
// can be safed, and -- having an output to energise -- it can say whether it
// is live. The second is a static_assert inside hal::energisedSourceAt() that
// fires at rig link time rather than here, so it is worth asserting locally
// too, where the diagnostic names this driver.
//
TEST( Wfg33522B, SatisfiesTheSafingAndInterlockContracts)
{
    static_assert( hal::SafeableInstrument<Wfg33522B> );
    static_assert( std::derived_from<Wfg33522B, hal::InstrumentTag> );

    static_assert( requires( Wfg33522B & w)       { w.removeOutput(); } );
    static_assert( requires( const Wfg33522B & w) { { w.isEnabled() } -> std::convertible_to<bool>; } );

    SUCCEED();
}

TEST( Wfg33522B, IsEnergisedFollowsTheOutputTheConfigNames)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    applied( wfg.channel<1>().sine().amplitude( 1.0_V));

    EXPECT_TRUE(  isEnergised( wfg.channel<1>().sine().config()));
    EXPECT_FALSE( isEnergised( wfg.channel<2>().sine().config()));
}

// ===========================================================================
// The journal line
// ===========================================================================

TEST( Wfg33522B, DescribeConfigNamesTheChannelAndTheShapeBeforeAnyNumbers)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    const auto described = describeConfig(
        wfg.channel<2>().square().frequency( 10.0_kHz).amplitude( 3.3_V).dutyCycle( 25.0).config());

    EXPECT_NE( described.Settings.find( "channel 2"),  std::string::npos) << described.Settings;
    EXPECT_NE( described.Settings.find( "square"),     std::string::npos) << described.Settings;
    EXPECT_NE( described.Settings.find( "dutyCycle"),  std::string::npos) << described.Settings;
    EXPECT_NE( described.Settings.find( "25"),         std::string::npos) << described.Settings;
}

TEST( Wfg33522B, DescribeConfigOmitsSettingsTheChainNeverNamed)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    const auto described = describeConfig( wfg.channel<1>().sine().amplitude( 1.0_V).config());

    EXPECT_EQ( described.Settings.find( "frequency"), std::string::npos) << described.Settings;
    EXPECT_EQ( described.Settings.find( "offset"),    std::string::npos) << described.Settings;
    EXPECT_EQ( described.Settings.find( "load"),      std::string::npos) << described.Settings;
}

// ===========================================================================
// What this model cannot produce
// ===========================================================================
//
// Every test here runs against a *simulated* instrument, which is the point:
// an attached 33522B answers most of these with a queued error and a clamped
// output rather than a refusal, so without these checks the same script would
// pass in CI and drive a DUT at the wrong frequency on the bench. See
// SettingOutOfRange's own comment.
//

TEST( Wfg33522BLimits, ARampAbove200kHzIsRefusedEvenThoughASineThatFastIsFine)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    EXPECT_NO_THROW( applied( wfg.channel<1>().sine().frequency( 20.0_MHz).amplitude( 1.0_V)));

    EXPECT_THROW( applied( wfg.channel<1>().ramp().frequency( 1.0_MHz).amplitude( 1.0_V)),
                  SettingOutOfRange);
}

TEST( Wfg33522BLimits, ASineAboveThisModelsThirtyMegahertzIsRefused)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    EXPECT_THROW( applied( wfg.channel<1>().sine().frequency( 40.0_MHz).amplitude( 1.0_V)),
                  SettingOutOfRange);
}

//
// The same amplitude is legal or not depending on the termination it is quoted
// against, which is why the two settings travel in one config -- see
// hal::keysight_33522b::Termination.
//
TEST( Wfg33522BLimits, AmplitudeIsBoundedByTheTerminationItIsQuotedAgainst)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    EXPECT_THROW( applied( wfg.channel<1>().sine().amplitude( 15.0_V).into( Termination::Ohms50)),
                  SettingOutOfRange);

    EXPECT_NO_THROW( applied( wfg.channel<1>().sine().amplitude( 15.0_V).into( Termination::HighImpedance)));
}

TEST( Wfg33522BLimits, AnAmplitudeBelowOneMillivoltIsRefused)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    EXPECT_THROW( applied( wfg.channel<1>().sine().amplitude( 0.0001_V)), SettingOutOfRange);
}

//
// The coupled limit: |offset| <= Vmax - Vpp/2. Both of these settings are
// legal alone and impossible together, which is exactly the case an attached
// instrument answers by moving the offset and carrying on.
//
TEST( Wfg33522BLimits, AnOffsetWithNoHeadroomLeftByTheAmplitudeIsRefused)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    // 4 V of offset alone is fine into 50 Ohm (the peak limit is 5 V) ...
    EXPECT_NO_THROW( applied( wfg.channel<1>().dc().offset( 4.0_V)));

    // ... and 8 Vpp of amplitude alone is fine (the limit is 10 Vpp) ...
    EXPECT_NO_THROW( applied( wfg.channel<1>().sine().amplitude( 8.0_V)));

    // ... but 4 V of offset on an 8 Vpp sine asks for a 8 V peak from an
    // output that stops at 5.
    EXPECT_THROW( applied( wfg.channel<1>().sine().amplitude( 8.0_V).offset( 4.0_V)),
                  SettingOutOfRange);
}

TEST( Wfg33522BLimits, ADutyCycleOutsideTheInstrumentsRangeIsRefused)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    EXPECT_THROW( applied( wfg.channel<1>().square().amplitude( 1.0_V).dutyCycle( 0.0)),
                  SettingOutOfRange);

    EXPECT_THROW( applied( wfg.channel<1>().square().amplitude( 1.0_V).dutyCycle( 100.0)),
                  SettingOutOfRange);

    EXPECT_NO_THROW( applied( wfg.channel<1>().square().amplitude( 1.0_V).dutyCycle( 0.01)));
}

TEST( Wfg33522BLimits, ASymmetryOutsideZeroToOneHundredIsRefused)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    EXPECT_THROW( applied( wfg.channel<1>().ramp().amplitude( 1.0_V).symmetry( 101.0)),
                  SettingOutOfRange);

    // Both ends are legal on a ramp -- 0% is a falling sawtooth, 100% a rising
    // one -- unlike a duty cycle, whose ends are not.
    EXPECT_NO_THROW( applied( wfg.channel<1>().ramp().amplitude( 1.0_V).symmetry(   0.0)));
    EXPECT_NO_THROW( applied( wfg.channel<1>().ramp().amplitude( 1.0_V).symmetry( 100.0)));
}

TEST( Wfg33522BLimits, TheRefusalNamesTheInstrumentTheSettingAndTheLimit)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    try
    {
        applied( wfg.channel<1>().ramp().frequency( 1.0_MHz).amplitude( 1.0_V));

        FAIL() << "expected a SettingOutOfRange";
    }
    catch( const SettingOutOfRange & refusal)
    {
        const std::string message{ refusal.what() };

        EXPECT_NE( message.find( to_string( anyId())), std::string::npos) << message;
        EXPECT_NE( message.find( "frequency"),         std::string::npos) << message;
        EXPECT_NE( message.find( "ramp"),              std::string::npos) << message;
    }
}

TEST( Wfg33522BLimits, ARefusedApplyLeavesTheOutputAsItWas)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    applied( wfg.channel<1>().sine().frequency( 1.0_kHz).amplitude( 2.0_V));

    EXPECT_THROW( applied( wfg.channel<1>().ramp().frequency( 1.0_MHz).amplitude( 1.0_V)),
                  SettingOutOfRange);

    EXPECT_EQ( wfg.function( 1), "sine");
    EXPECT_DOUBLE_EQ( wfg.frequency( 1)->value(), 1000.0);
    EXPECT_DOUBLE_EQ( wfg.amplitude( 1)->value(), 2.0);
}

//
// ===========================================================================
// The wire: what this generator is actually told
// ===========================================================================
//
// Everything above tests the driver's simulated half, which is what every
// script test in this repository drives through. This half tests the other
// one: the driver with a transport handed in, asserting the exact strings that
// went down it. Nothing here opens a socket.
//

namespace
{
    class FakeGenerator final : public hal::io::ITransport
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
                else if( command.starts_with( "OUTP") && command.ends_with( "?"))
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
                    throw hal::io::TransportTimeout( "nothing queued on the fake generator");
                }

                std::string reply = mReplies.front();

                mReplies.erase( mReplies.begin());

                return reply;
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "fake 33522B";
            }

            [[nodiscard]]
            auto sent() const -> const std::vector<std::string> &
            {
                return mSent;
            }

            std::string              Identity{ "Keysight Technologies,33522B,MY59003130,0.179-1.19-8.88-52-00" };
            std::string              OutputState{ "1" };
            std::vector<std::string> Errors;

        private:
            std::vector<std::string> mSent;
            std::vector<std::string> mReplies;
    };

    //
    // A generator with a fake transport already installed, and the fake still
    // reachable. A struct rather than two locals because the driver takes
    // ownership of the transport (see Wfg33522B::useTransport) and the test
    // still needs to read what was sent through it.
    //
    struct Bench
    {
        Wfg33522B       Generator{ anyId(), hal::Simulated{} };
        FakeGenerator * Wire{};
    };

    [[nodiscard]]
    auto attached() -> std::unique_ptr<Bench>
    {
        auto bench = std::make_unique<Bench>();
        auto fake  = std::make_unique<FakeGenerator>();

        bench->Wire = fake.get();

        bench->Generator.useTransport( std::move( fake));

        return bench;
    }

    //
    // Everything the driver sent, with the session-opening exchange dropped --
    // the error-queue drain and the identity query, which happen once and which
    // most of these tests are not about. Asserted in full by the one test that
    // is about them.
    //
    [[nodiscard]]
    auto afterOpening( const FakeGenerator & wire) -> std::vector<std::string>
    {
        auto commands = wire.sent();

        commands.erase( commands.begin(), commands.begin() + 2);

        return commands;
    }

    //
    // The same, with the SYST:ERR? after every command stripped out. The
    // per-command drain is asserted by its own test; the ordering tests below
    // are about the commands, and interleaving twelve identical queries
    // through them would hide the sequence they exist to check.
    //
    [[nodiscard]]
    auto commandsOnly( const FakeGenerator & wire) -> std::vector<std::string>
    {
        std::vector<std::string> commands;

        for( const auto & sent : afterOpening( wire))
        {
            if( sent != "SYST:ERR?")
            {
                commands.emplace_back( sent);
            }
        }

        return commands;
    }
} // namespace

//
// A transport wins over the address it was not opened from -- which is what
// makes every test below possible, and is stated as its own assertion so that
// the reason the rest of them work is written down once.
//
TEST( Wfg33522BWire, AnInjectedTransportMakesTheDriverStopSimulating)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    EXPECT_TRUE( wfg.isSimulated());

    wfg.useTransport( std::make_unique<FakeGenerator>());

    EXPECT_FALSE( wfg.isSimulated());
}

TEST( Wfg33522BWire, OpeningASessionDrainsTheErrorQueueThenAsksWhatTheInstrumentIs)
{
    auto bench = attached();

    static_cast<void>( bench->Generator.session());

    ASSERT_GE( bench->Wire->sent().size(), 2u);

    EXPECT_EQ( bench->Wire->sent()[ 0], "SYST:ERR?");
    EXPECT_EQ( bench->Wire->sent()[ 1], "*IDN?");
}

//
// The ordering test, and the most load-bearing one in this file: every step in
// src/keysight_33522b.cpp's program() is justified by what the step after it
// would otherwise mean, so the sequence is the driver's actual contract.
//
TEST( Wfg33522BWire, ApplySendsLoadThenFunctionThenFrequencyThenShapeThenAmplitudeThenOffsetThenOutputOn)
{
    auto bench = attached();

    applied( bench->Generator.channel<1>().square()
               .frequency( 10.0_kHz)
               .amplitude( 3.3_V)
               .offset( 0.5_V)
               .dutyCycle( 25.0)
               .into( Termination::HighImpedance));

    EXPECT_EQ( commandsOnly( *bench->Wire),
               ( std::vector<std::string>{
                   "OUTP1:LOAD INF",
                   "SOUR1:FUNC SQU",
                   "SOUR1:FREQ 10000",
                   "SOUR1:FUNC:SQU:DCYC 25",
                   "SOUR1:VOLT 3.3 VPP",
                   "SOUR1:VOLT:OFFS 0.5",
                   "OUTP1 ON",
                   "*OPC?" }));
}

TEST( Wfg33522BWire, EveryCommandCarriesTheChannelTheConfigNamed)
{
    auto bench = attached();

    applied( bench->Generator.channel<2>().ramp().frequency( 100.0_Hz).amplitude( 1.0_V).symmetry( 50.0));

    EXPECT_EQ( commandsOnly( *bench->Wire),
               ( std::vector<std::string>{
                   "SOUR2:FUNC RAMP",
                   "SOUR2:FREQ 100",
                   "SOUR2:FUNC:RAMP:SYMM 50",
                   "SOUR2:VOLT 1 VPP",
                   "OUTP2 ON",
                   "*OPC?" }));
}

//
// The two shapes that have a duty cycle reach two different commands, which is
// why the command is on the shape tag rather than written once in the .cpp.
//
TEST( Wfg33522BWire, APulsesDutyCycleIsADifferentCommandFromASquares)
{
    auto bench = attached();

    applied( bench->Generator.channel<1>().pulse().frequency( 1.0_kHz).amplitude( 1.0_V).dutyCycle( 10.0));

    const auto commands = commandsOnly( *bench->Wire);

    EXPECT_NE( std::find( commands.begin(), commands.end(), "SOUR1:FUNC:PULS:DCYC 10"), commands.end());
    EXPECT_EQ( std::find( commands.begin(), commands.end(), "SOUR1:FUNC:SQU:DCYC 10"),  commands.end());
}

//
// The amplitude carries its own unit rather than relying on VOLT:UNIT, which
// is state some other user of this box may have left anywhere -- see
// program()'s own comment.
//
TEST( Wfg33522BWire, TheAmplitudeCarriesItsUnitAndTheDriverNeverSendsVoltUnit)
{
    auto bench = attached();

    applied( bench->Generator.channel<1>().sine().amplitude( 2.5_V));

    const auto commands = commandsOnly( *bench->Wire);

    EXPECT_NE( std::find( commands.begin(), commands.end(), "SOUR1:VOLT 2.5 VPP"), commands.end());

    for( const auto & command : commands)
    {
        EXPECT_EQ( command.find( "VOLT:UNIT"), std::string::npos) << command;
    }
}

//
// A termination the config did not name is not sent at all -- the instrument
// stays in whichever one it is in, which is the same thing nullopt means for
// every other setting here.
//
TEST( Wfg33522BWire, AConfigThatNamesNoTerminationSendsNoLoadCommand)
{
    auto bench = attached();

    applied( bench->Generator.channel<1>().sine().amplitude( 1.0_V));

    for( const auto & command : commandsOnly( *bench->Wire))
    {
        EXPECT_EQ( command.find( "LOAD"), std::string::npos) << command;
    }
}

//
// APPLy would have been shorter and is deliberately not used: it discards the
// duty cycle and the symmetry, enables the output as part of the same command,
// and silently turns off any modulation or burst in force. See the .cpp's own
// comment. *RST is out for the neighbouring reason -- it would reset the other
// channel too.
//
TEST( Wfg33522BWire, NothingThisDriverSendsIsApplyOrResetsTheInstrument)
{
    auto bench = attached();

    applied( bench->Generator.channel<1>().square().frequency( 1.0_kHz).amplitude( 1.0_V).dutyCycle( 30.0));
    removed( bench->Generator.channel<1>().square());

    for( const auto & command : bench->Wire->sent())
    {
        EXPECT_EQ( command.find( "APPL"), std::string::npos) << command;
        EXPECT_EQ( command.find( "*RST"), std::string::npos) << command;
    }
}

TEST( Wfg33522BWire, EveryConfigureCommandIsFollowedByAnErrorQueueRead)
{
    auto bench = attached();

    applied( bench->Generator.channel<1>().sine().frequency( 1.0_kHz).amplitude( 1.0_V));

    const auto sent = afterOpening( *bench->Wire);

    // FUNC, FREQ, VOLT, OUTP ON -- each checked -- then *OPC?.
    EXPECT_EQ( sent, ( std::vector<std::string>{
                   "SOUR1:FUNC SIN", "SYST:ERR?",
                   "SOUR1:FREQ 1000", "SYST:ERR?",
                   "SOUR1:VOLT 1 VPP", "SYST:ERR?",
                   "OUTP1 ON", "SYST:ERR?",
                   "*OPC?" }));
}

TEST( Wfg33522BWire, RemoveTurnsTheOutputOffAndWaitsForIt)
{
    auto bench = attached();

    removed( bench->Generator.channel<2>().sine());

    EXPECT_EQ( commandsOnly( *bench->Wire), ( std::vector<std::string>{ "OUTP2 OFF", "*OPC?" }));
}

TEST( Wfg33522BWire, OutputIsOnAsksTheInstrument)
{
    auto bench = attached();

    bench->Wire->OutputState = "0";

    EXPECT_FALSE( bench->Generator.outputIsOn( 1));

    bench->Wire->OutputState = "1";

    EXPECT_TRUE( bench->Generator.outputIsOn( 1));

    const auto commands = commandsOnly( *bench->Wire);

    EXPECT_EQ( commands, ( std::vector<std::string>{ "OUTP1?", "OUTP1?" }));
}

//
// The case the round trip exists for: an output this process never turned on.
// A driver reading back its own memory would call this cold.
//
TEST( Wfg33522BWire, IsEnergisedSaysLiveEvenWhenThisDriverNeverTurnedTheOutputOn)
{
    auto bench = attached();

    bench->Wire->OutputState = "1";

    EXPECT_FALSE( bench->Generator.isEnabled( 1));            // this driver's memory
    EXPECT_TRUE(  isEnergised( bench->Generator.channel<1>().sine().config()));  // the instrument
}

TEST( Wfg33522BWire, SafeTurnsBothOutputsOffThenCollapsesTheirSignals)
{
    auto bench = attached();

    applied( bench->Generator.channel<1>().sine().amplitude( 5.0_V));

    bench->Generator.safe();

    const auto commands = commandsOnly( *bench->Wire);

    ASSERT_GE( commands.size(), 6u);

    // Off first on each channel, then the amplitude and offset -- see
    // sendSafe(), which is deliberately the reverse of the guide's
    // glitch-avoidance advice.
    EXPECT_EQ( commands[ commands.size() - 6], "OUTP1 OFF");
    EXPECT_EQ( commands[ commands.size() - 5], "SOUR1:VOLT MIN");
    EXPECT_EQ( commands[ commands.size() - 4], "SOUR1:VOLT:OFFS 0");
    EXPECT_EQ( commands[ commands.size() - 3], "OUTP2 OFF");
    EXPECT_EQ( commands[ commands.size() - 2], "SOUR2:VOLT MIN");
    EXPECT_EQ( commands[ commands.size() - 1], "SOUR2:VOLT:OFFS 0");
}

//
// VOLT MIN rather than VOLT 0: zero is not a legal amplitude on this
// instrument, and on the safing path a refusal would be silent (write() does
// not read the queue).
//
TEST( Wfg33522BWire, SafeNeverAsksForAnAmplitudeOfZero)
{
    auto bench = attached();

    bench->Generator.safe();

    for( const auto & command : bench->Wire->sent())
    {
        EXPECT_EQ( command.find( "VOLT 0"), std::string::npos)
            << "safing asked for an amplitude this instrument would refuse: " << command;
    }
}

TEST( Wfg33522BWire, SafeOnANeverUsedGeneratorOpensNothing)
{
    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    // No transport, no session, and safing must not try to make one -- see
    // Wfg33522B::safe().
    EXPECT_NO_THROW( wfg.safe());
}

TEST( Wfg33522BWire, ASafingSendThatFailsIsSwallowed)
{
    //
    // A transport that throws on every send, which is what an unreachable
    // instrument looks like from here. safeRig() does not catch, so a throw
    // out of safe() would abandon the safing of every instrument after this
    // one.
    //
    class DeadTransport final : public hal::io::ITransport
    {
        public:
            auto send( std::string_view) -> void override
            {
                throw hal::io::TransportError( "the generator is gone");
            }

            auto receive() -> std::string override
            {
                throw hal::io::TransportError( "the generator is gone");
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "dead";
            }
    };

    Wfg33522B wfg{ anyId(), hal::Simulated{} };

    wfg.useTransport( std::make_unique<DeadTransport>());

    EXPECT_NO_THROW( wfg.safe());
}

// ===========================================================================
// The identity check
// ===========================================================================

TEST( Wfg33522BWire, AnInstrumentThatIsNotThisModelIsRefused)
{
    auto bench = attached();

    bench->Wire->Identity = "Keysight Technologies,EDU36311A,CN61130007,1.0.2-1.0.1";

    EXPECT_THROW( static_cast<void>( bench->Generator.session()), hal::io::ScpiFault);
}

//
// The refusal that matters most: another member of this series speaks the same
// commands, so nothing this driver sends would come back as an error. Only the
// identity check stands between a 33521B and a rig that believes it has two
// channels.
//
TEST( Wfg33522BWire, AnotherMemberOfTheSeriesIsRefusedEvenThoughItSpeaksTheSameCommands)
{
    auto bench = attached();

    bench->Wire->Identity = "Keysight Technologies,33521B,MY59003130,0.179-1.19-8.88-52-00";

    EXPECT_THROW( static_cast<void>( bench->Generator.session()), hal::io::ScpiFault);
}

//
// This series launched as an Agilent product, so the same instrument answers
// with either manufacturer depending on when it was built. Both are this box,
// which is why the check reads the model field and not the whole string.
//
TEST( Wfg33522BWire, AnAgilentBadgedUnitIsAccepted)
{
    auto bench = attached();

    bench->Wire->Identity = "Agilent Technologies,33522B,MY50000123,1.12-1.19-52-00";

    EXPECT_NO_THROW( static_cast<void>( bench->Generator.session()));
}

TEST( Wfg33522BWire, AnIdentityThatIsNotShapedLikeAnIdnReplyIsRefused)
{
    auto bench = attached();

    bench->Wire->Identity = "READY";

    EXPECT_THROW( static_cast<void>( bench->Generator.session()), hal::io::ScpiFault);
}

TEST( Wfg33522BWire, AFailedIdentityCheckIsRetriedRatherThanRemembered)
{
    auto bench = attached();

    bench->Wire->Identity = "Keysight Technologies,33521B,MY59003130,1.0";

    EXPECT_THROW( static_cast<void>( bench->Generator.session()), hal::io::ScpiFault);

    //
    // The instrument that was wrong when the run started may be the right one
    // now -- somebody re-cabled the rack. Asked again rather than treated as
    // verified or as permanently refused.
    //
    bench->Wire->Identity = "Keysight Technologies,33522B,MY59003130,1.0";

    EXPECT_NO_THROW( static_cast<void>( bench->Generator.session()));
}

// ===========================================================================
// What the instrument refuses
// ===========================================================================

TEST( Wfg33522BWire, ASettingTheInstrumentRefusesStopsTheApplyBeforeTheOutputComesOn)
{
    auto bench = attached();

    //
    // The drain that opens the session takes the first entry, so the refusal
    // has to be queued behind it -- see the ScpiSession note about a session
    // draining the queue when it opens.
    //
    static_cast<void>( bench->Generator.session());

    bench->Wire->Errors = { "-222,\"Data out of range\"" };

    EXPECT_THROW( applied( bench->Generator.channel<1>().sine().frequency( 1.0_kHz).amplitude( 1.0_V)),
                  hal::io::ScpiFault);

    const auto & sent = bench->Wire->sent();

    EXPECT_EQ( std::find( sent.begin(), sent.end(), "OUTP1 ON"), sent.end())
        << "the output came on after a command the instrument had refused";
}

TEST( Wfg33522BWire, ARefusedCommandIsReportedWithTheCommandThatCausedIt)
{
    auto bench = attached();

    static_cast<void>( bench->Generator.session());

    bench->Wire->Errors = { "-222,\"Data out of range\"" };

    try
    {
        applied( bench->Generator.channel<1>().sine().frequency( 1.0_kHz).amplitude( 1.0_V));

        FAIL() << "expected an ScpiFault";
    }
    catch( const hal::io::ScpiFault & fault)
    {
        EXPECT_EQ( fault.command(), "SOUR1:FUNC SIN");
        EXPECT_EQ( fault.error().Code, -222);
    }
}

//
// A refusal this driver makes itself never reaches the wire at all, which is
// the difference between a limit check and an error queue read.
//
TEST( Wfg33522BWire, ASettingOutOfRangeRefusalSendsNothing)
{
    auto bench = attached();

    EXPECT_THROW( applied( bench->Generator.channel<1>().ramp().frequency( 1.0_MHz).amplitude( 1.0_V)),
                  SettingOutOfRange);

    EXPECT_TRUE( bench->Wire->sent().empty());
}
