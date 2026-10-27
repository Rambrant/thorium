#include "hal/pico_5244d.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "hal/pico_5244d/measurements.hpp"

namespace hal::pico_5244d
{
    using detail::check;

    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr auto kPollInterval = std::chrono::milliseconds( 1);

        // Margin on top of the pre-trigger fill before Arm calls the unit ready.
        constexpr auto kArmMargin = std::chrono::milliseconds( 5);

        // The divisions across the window, as on the DSOX1202G, so a script's
        // timePerDivision means the same on both.
        constexpr double kDivisions = 10.0;

        // PS5000A_RANGE: 0 is +/-10 mV and each step is the 1-2-5 sequence.
        // The 5244D stops at +/-20 V (the enum has a 50 V entry that 5000D
        // hardware does not).
        constexpr std::array<double, 11> kRanges{ 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0 };

        // PS5000A_COUPLING, PS5000A_BANDWIDTH_LIMITER, PS5000A_THRESHOLD_DIRECTION.
        constexpr std::int32_t kCouplingAc        = 0;
        constexpr std::int32_t kCouplingDc        = 1;
        constexpr std::int32_t kBandwidthFull     = 0;
        constexpr std::int32_t kBandwidth20Mhz    = 1;
        constexpr std::int32_t kDirectionRising   = 2;
        constexpr std::int32_t kDirectionFalling  = 3;
        constexpr std::int32_t kDirectionEither   = 4;

        [[nodiscard]]
        auto picoCoupling( const Coupling value) -> std::int32_t
        {
            return value == Coupling::Ac ? kCouplingAc : kCouplingDc;
        }

        [[nodiscard]]
        auto picoDirection( const TriggerSlope value) -> std::int32_t
        {
            switch( value)
            {
                case TriggerSlope::Rising:  return kDirectionRising;
                case TriggerSlope::Falling: return kDirectionFalling;
                case TriggerSlope::Either:  return kDirectionEither;
            }

            return kDirectionRising;
        }

        [[nodiscard]]
        auto referenceFraction( const TimebaseReference value) -> double
        {
            switch( value)
            {
                case TimebaseReference::Left:   return 0.0;
                case TimebaseReference::Center: return 0.5;
                case TimebaseReference::Right:  return 1.0;
            }

            return 0.5;
        }

        // The smallest range holding +/-`volts` at the instrument's input.
        [[nodiscard]]
        auto rangeIndexFor( const double volts, const std::string & instrument, const double attenuation) -> std::int32_t
        {
            for( std::size_t index = 0; index < kRanges.size(); ++index)
            {
                // A tolerance for 0.1 * 10 not being exactly 1.
                if( volts <= kRanges[ index] * ( 1.0 + 1e-9))
                {
                    return static_cast<std::int32_t>( index);
                }
            }

            throw SettingOutOfRange( instrument, "range",
                "of +/-" + std::to_string( volts * attenuation) + " V needs more than the +/-"
                + std::to_string( kRanges.back() * attenuation) + " V the input takes at a probe ratio of "
                + std::to_string( attenuation) + ":1");
        }
    } // namespace

    //
    // What one block capture is going to be: the SDK's timebase number and
    // sample counts, and where the first sample sits relative to the trigger.
    //
    struct Plan
    {
        std::uint32_t Timebase{};
        double        Interval{};   // seconds
        std::int32_t  Pre{};
        std::int32_t  Post{};
        std::uint32_t Delay{};      // samples after the trigger before the first
        double        Origin{};     // seconds from the trigger to the first sample

        [[nodiscard]]
        auto samples() const -> std::int32_t
        {
            return Pre + Post;
        }
    };

    //
    // A block capture in flight: the plan, and the buffers the SDK is
    // filling. Buffers live here because the SDK holds raw pointers into them
    // from SetDataBuffer until GetValues.
    //
    struct Oscilloscope::Pending
    {
        Plan                                    Shape;
        std::array<std::vector<std::int16_t>, channel_count> Buffers;
        std::array<bool, channel_count>         Enabled{};
    };

    //
    // The result of a capture: a record per enabled channel, in tip volts, and
    // whether each overflowed its range.
    //
    struct Oscilloscope::Capture
    {
        std::array<std::optional<core::Waveform>, channel_count> Traces;
        std::array<bool, channel_count>                          Clipped{};
    };

    auto Oscilloscope::session() -> Unit &
    {
        auto & unit = detail::openedUnit( mConnection, mResolution);

        if( unit.Generation == mAppliedTo)
        {
            return unit;
        }

        //
        // A unit this face has not set up: the first open, or one reopened
        // after closeSession() -- which starts from the unit's defaults, so
        // whatever the last Setup said is applied again.
        //
        resetCapture();

        try
        {
            static_cast<void>( applyResolution( unit));

            pushAllChannels( unit);
        }
        catch( ...)
        {
            detail::closeUnit( mConnection);

            throw;
        }

        mAppliedTo = unit.Generation;

        return unit;
    }

    auto Oscilloscope::applyResolution( Unit & unit) -> bool
    {
        if( !mResolution || unit.Bits == *mResolution)
        {
            return false;
        }

        check( "SetDeviceResolution",
               unit.Api->setDeviceResolution( unit.Handle, static_cast<std::int32_t>( *mResolution)));

        unit.Bits = *mResolution;

        return true;
    }

    auto Oscilloscope::identity() -> std::string
    {
        return detail::identityOf( session());
    }

    auto Oscilloscope::resetCapture() -> void
    {
        mPending.reset();
        mHeld.reset();
        mArmed     = false;
        mCompleted = false;
        mApplied   = {};
        mAppliedTo = 0;
    }

    auto Oscilloscope::closeSession() -> void
    {
        resetCapture();

        // The Unit's destructor stops and closes the hardware.
        detail::closeUnit( mConnection);
    }

    auto Oscilloscope::invalidate() -> void
    {
        mHeld.reset();
        mCompleted = false;
    }

    auto Oscilloscope::safe() -> void
    {
        mArmed = false;

        invalidate();

        mPending.reset();

        //
        // Only down a unit that is already open, and never raising: see the
        // header. A unit left running a block would trigger on the next
        // script's stimulus and answer its Await.
        //
        if( auto * const open = detail::heldUnit( mConnection))
        {
            try
            {
                static_cast<void>( open->Api->stop( open->Handle));
            }
            catch( ...)
            {
            }
        }
    }

    auto Oscilloscope::pushChannel( Unit & open, const unsigned channel) -> void
    {
        auto & state = atChannel( channel);

        // No range, no channel: there is nothing to enable it *at*.
        if( !state.Range)
        {
            mApplied.at( channel - 1).reset();

            return;
        }

        const std::string name        = "channel " + std::to_string( channel);
        const double      attenuation = state.ProbeAttenuation.value_or( 1.0);
        const double      offsetTip   = state.VerticalOffset ? state.VerticalOffset->value() : 0.0;
        const auto        coupling    = state.InputCoupling.value_or( Coupling::Dc);

        const auto rangeIndex = rangeIndexFor( state.Range->value() / attenuation, name, attenuation);

        // + 0.0 turns a negative zero (from negating 0 V) into a plain one.
        const auto picoOffset = -offsetTip / attenuation + 0.0;

        //
        // The offset checked against what the SDK says this range allows, so a
        // request outside it is refused by name here rather than clamped by the
        // driver library. (The limits are per range and coupling, and differ
        // between ranges: asked, not tabulated.)
        //
        float maximum = 0.0F;
        float minimum = 0.0F;

        if( open.Api->analogueOffsetLimits( open.Handle, rangeIndex, picoCoupling( coupling), maximum, minimum) == kPicoOk
            && ( picoOffset > static_cast<double>( maximum) + 1e-6 || picoOffset < static_cast<double>( minimum) - 1e-6))
        {
            throw SettingOutOfRange( name, "verticalOffset",
                "of " + std::to_string( offsetTip) + " V is outside what the +/-"
                + std::to_string( kRanges[ static_cast<std::size_t>( rangeIndex)] * attenuation)
                + " V range allows (" + std::to_string( -static_cast<double>( maximum) * attenuation) + " to "
                + std::to_string( -static_cast<double>( minimum) * attenuation) + " V)");
        }

        const bool enabled = state.Display.value_or( ChannelDisplay::On) == ChannelDisplay::On;

        check( "SetChannel", open.Api->setChannel( open.Handle, static_cast<std::int32_t>( channel - 1), enabled,
                                                   picoCoupling( coupling), rangeIndex, static_cast<float>( picoOffset)));

        if( state.BandwidthLimit)
        {
            check( "SetBandwidthFilter",
                   open.Api->setBandwidthFilter( open.Handle, static_cast<std::int32_t>( channel - 1),
                                                 *state.BandwidthLimit == Bandwidth::Limited ? kBandwidth20Mhz : kBandwidthFull));
        }

        if( enabled)
        {
            mApplied.at( channel - 1) = Scaling{ kRanges[ static_cast<std::size_t>( rangeIndex)], picoOffset, attenuation };
        }
        else
        {
            mApplied.at( channel - 1).reset();
        }
    }

    auto Oscilloscope::pushAllChannels( Unit & open) -> void
    {
        for( unsigned channel = 1; channel <= channel_count; ++channel)
        {
            pushChannel( open, channel);
        }
    }

    auto Oscilloscope::configureChannel( const ChannelConfig & config) -> void
    {
        auto & state = atChannel( config.Channel);

        if( config.ProbeAttenuation && !( *config.ProbeAttenuation > 0.0))
        {
            throw SettingOutOfRange( "channel " + std::to_string( config.Channel), "probeAttenuation",
                                     "must be a positive ratio");
        }

        if( config.Range && !( config.Range->value() > 0.0))
        {
            throw SettingOutOfRange( "channel " + std::to_string( config.Channel), "range", "must be positive");
        }

        if( config.InputCoupling)    { state.InputCoupling    = config.InputCoupling;    }
        if( config.Range)            { state.Range            = config.Range;            }
        if( config.VerticalOffset)   { state.VerticalOffset   = config.VerticalOffset;   }
        if( config.BandwidthLimit)   { state.BandwidthLimit   = config.BandwidthLimit;   }
        if( config.ProbeAttenuation) { state.ProbeAttenuation = config.ProbeAttenuation; }
        if( config.Display)          { state.Display          = config.Display;          }

        invalidate();

        if( isSimulated())
        {
            return;
        }

        // Probe ratio, range and offset are one conversion, so the whole
        // channel is pushed rather than only the fields this Setup named: a
        // new probe ratio changes the range the SDK is asked for.
        pushChannel( session(), config.Channel);
    }

    auto Oscilloscope::configureTrigger( const TriggerConfig & config) -> void
    {
        if( config.EdgeSource) { mTriggerSource = config.EdgeSource; }
        if( config.Slope)      { mTriggerSlope  = config.Slope;      }
        if( config.Level)      { mTriggerLevel  = config.Level;      }
        if( config.Sweep)      { mTriggerSweep  = config.Sweep;      }

        invalidate();
    }

    auto Oscilloscope::configureTimebase( const TimebaseConfig & config) -> void
    {
        if( config.TimePerDivision && !( config.TimePerDivision->value() > 0.0))
        {
            throw SettingOutOfRange( "timebase", "timePerDivision", "must be positive");
        }

        if( config.Points && *config.Points < 2)
        {
            throw SettingOutOfRange( "timebase", "points", "must be at least 2");
        }

        if( config.TimePerDivision) { mTimePerDivision   = config.TimePerDivision; }
        if( config.Position)        { mTimebasePosition  = config.Position;        }
        if( config.Reference)       { mTimebaseReference = config.Reference;       }
        if( config.Points)          { mPoints            = config.Points;          }

        invalidate();
    }

    auto Oscilloscope::configureAcquisition( const AcquisitionConfig & config) -> void
    {
        if( config.Bits)
        {
            mResolution = config.Bits;
        }

        invalidate();

        if( isSimulated() || !config.Bits)
        {
            return;
        }

        auto & open = session();

        // The new resolution has its own timebases and its own channel limits.
        if( applyResolution( open))
        {
            pushAllChannels( open);
        }
    }

    namespace
    {
        //
        // The timebase number whose sample interval is the largest that does
        // not exceed `target`, or the finest the unit offers if even that is
        // coarser.
        //
        // Searched through GetTimebase2 rather than computed, because the
        // number-to-interval mapping differs by resolution (and by how many
        // channels are on) and every formula for it is a place to be wrong
        // without hardware to say so. The search assumes what the SDK
        // documents: valid timebases start at some number, and the interval
        // never decreases as the number rises. It costs a few dozen cheap
        // calls, once per capture.
        //
        [[nodiscard]]
        auto findTimebase( Ps5000a & api, const std::int16_t handle, const double target) -> std::pair<std::uint32_t, double>
        {
            const auto probe = [ & ]( const std::uint32_t timebase) -> std::optional<double>
            {
                float        interval   = 0.0F;
                std::int32_t maxSamples = 0;

                if( api.timebase( handle, timebase, 1, interval, maxSamples) != kPicoOk)
                {
                    return std::nullopt;
                }

                return static_cast<double>( interval) * 1e-9;
            };

            // The first valid timebase: the low numbers are refused at the
            // higher resolutions.
            std::uint32_t first = 0;

            std::optional<double> finest;

            for( ; first <= 32; ++first)
            {
                finest = probe( first);

                if( finest)
                {
                    break;
                }
            }

            if( !finest)
            {
                throw PicoFault( "GetTimebase2", 0x0000000E, "no timebase between 0 and 32 is valid at this resolution");
            }

            if( *finest > target)
            {
                return { first, *finest };
            }

            const auto fits = [ & ]( const std::uint32_t timebase) -> std::optional<double>
            {
                const auto interval = probe( timebase);

                return interval && *interval <= target ? interval : std::nullopt;
            };

            // Grow until it stops fitting, then bisect the last doubling.
            std::uint32_t good     = first;
            double        goodTime = *finest;
            std::uint32_t bad      = first + 1;

            constexpr std::uint32_t kCeiling = 1U << 30;

            while( bad < kCeiling)
            {
                if( const auto interval = fits( bad))
                {
                    good     = bad;
                    goodTime = *interval;
                    bad     *= 2;
                }
                else
                {
                    break;
                }
            }

            while( bad - good > 1)
            {
                const std::uint32_t middle = good + ( bad - good) / 2;

                if( const auto interval = fits( middle))
                {
                    good     = middle;
                    goodTime = *interval;
                }
                else
                {
                    bad = middle;
                }
            }

            return { good, goodTime };
        }
    } // namespace

    namespace
    {
        [[nodiscard]]
        auto describeTime( const double seconds) -> std::string
        {
            return core::describeValue( core::quantities::Time{ seconds });
        }
    } // namespace

    auto Oscilloscope::simulatedCapture() -> std::shared_ptr<const Capture>
    {
        auto result = std::make_shared<Capture>();

        for( unsigned channel = 1; channel <= channel_count; ++channel)
        {
            if( !atChannel( channel).Trace.empty())
            {
                result->Traces.at( channel - 1) = atChannel( channel).Trace;
            }
        }

        return result;
    }

    auto Oscilloscope::planCapture( Unit & open) -> Plan
    {
        if( !mTimePerDivision)
        {
            throw NotSetUp( "no timebase: Setup( Osc.timebase().timePerDivision( ...)) before a capture");
        }

        const double window = mTimePerDivision->value() * kDivisions;
        const double points = static_cast<double>( mPoints.value_or( kDefaultPoints));

        const auto [ timebase, interval] = findTimebase( *open.Api, open.Handle, window / points);

        const auto samples = static_cast<std::int32_t>( std::max<long long>( 2, std::llround( window / interval)));

        //
        // The first sample's time relative to the trigger: the window starts
        // `fraction` of its width before the reference point, and the reference
        // point is `position` after the trigger. Both are the DSOX1202G's
        // meanings, so a script's reference and position move across unchanged.
        //
        const double position = mTimebasePosition ? mTimebasePosition->value() : 0.0;
        const double fraction = referenceFraction( mTimebaseReference.value_or( TimebaseReference::Center));
        const double first    = position - fraction * window;

        Plan plan;

        plan.Timebase = timebase;
        plan.Interval = interval;

        if( first >= 0.0)
        {
            // The whole window is after the trigger: that is the SDK's delay.
            plan.Pre    = 0;
            plan.Post   = samples;
            plan.Delay  = static_cast<std::uint32_t>( std::llround( first / interval));
            plan.Origin = static_cast<double>( plan.Delay) * interval;
        }
        else
        {
            plan.Pre    = std::min( samples - 1, static_cast<std::int32_t>( std::llround( -first / interval)));
            plan.Post   = samples - plan.Pre;
            plan.Origin = -static_cast<double>( plan.Pre) * interval;
        }

        //
        // Whether the unit can hold that many samples with this many channels
        // on -- the memory is shared -- asked of the SDK, which knows.
        //
        float        reported   = 0.0F;
        std::int32_t maxSamples = 0;

        check( "GetTimebase2", open.Api->timebase( open.Handle, plan.Timebase, plan.samples(), reported, maxSamples));

        if( plan.samples() > maxSamples)
        {
            throw SettingOutOfRange( "timebase", "window",
                "of " + describeTime( window) + " at " + describeTime( interval) + " a sample needs "
                + std::to_string( plan.samples()) + " samples and the unit holds " + std::to_string( maxSamples)
                + " with the channels now on -- shorten the window or ask for fewer points");
        }

        return plan;
    }

    auto Oscilloscope::beginCapture( Unit & open, const bool triggered) -> std::shared_ptr<Pending>
    {
        auto pending = std::make_shared<Pending>();

        pending->Shape = planCapture( open);

        std::optional<unsigned> first;

        for( unsigned channel = 1; channel <= channel_count; ++channel)
        {
            if( mApplied.at( channel - 1))
            {
                pending->Enabled.at( channel - 1) = true;

                if( !first)
                {
                    first = channel;
                }
            }
        }

        if( !first)
        {
            throw NotSetUp( "no channel is set up: Setup( Osc.channel<N>().range( ...)) before a capture");
        }

        // The buffers first, and kept in `pending` for as long as the SDK
        // holds their addresses.
        for( unsigned channel = 1; channel <= channel_count; ++channel)
        {
            if( pending->Enabled.at( channel - 1))
            {
                auto & buffer = pending->Buffers.at( channel - 1);

                buffer.assign( static_cast<std::size_t>( pending->Shape.samples()), 0);

                check( "SetDataBuffer", open.Api->setDataBuffer( open.Handle, static_cast<std::int32_t>( channel - 1),
                                                                 buffer.data(), pending->Shape.samples()));
            }
        }

        if( triggered)
        {
            const unsigned source = mTriggerSource.value_or( *first);

            if( !mApplied.at( source - 1))
            {
                throw NotSetUp( "the trigger is on channel " + std::to_string( source)
                                + ", which is not set up (or is switched off)");
            }

            //
            // The level in ADC counts. The comparator sees the digitised
            // signal, so the level is expressed against the channel's range
            // and offset at the instrument's input: (level - offset) / probe /
            // range, as a fraction of full scale. Clamped to the ADC's span; a
            // level beyond the range then triggers on the first or last code
            // rather than wrapping.
            //
            const auto & scale = *mApplied.at( source - 1);

            const double level    = mTriggerLevel ? mTriggerLevel->value() : 0.0;
            const double fraction = ( level / scale.Attenuation + scale.PicoOffset) / scale.Range;
            const double counts   = std::clamp( fraction * static_cast<double>( open.AdcMax),
                                                -static_cast<double>( open.AdcMax), static_cast<double>( open.AdcMax));

            const auto autoMs = static_cast<std::int16_t>(
                mTriggerSweep.value_or( TriggerSweep::Auto) == TriggerSweep::Auto ? kAutoTriggerMs : 0);

            check( "SetSimpleTrigger",
                   open.Api->setSimpleTrigger( open.Handle, true, static_cast<std::int32_t>( source - 1),
                                               static_cast<std::int16_t>( std::lround( counts)),
                                               picoDirection( mTriggerSlope.value_or( TriggerSlope::Rising)),
                                               pending->Shape.Delay, autoMs));
        }
        else
        {
            // No trigger: the block simply runs. Its time zero is arbitrary.
            check( "SetSimpleTrigger",
                   open.Api->setSimpleTrigger( open.Handle, false, static_cast<std::int32_t>( *first - 1), 0,
                                               kDirectionRising, 0, 0));
        }

        std::int32_t indisposed = 0;

        check( "RunBlock", open.Api->runBlock( open.Handle, pending->Shape.Pre, pending->Shape.Post,
                                               pending->Shape.Timebase, indisposed));

        return pending;
    }

    auto Oscilloscope::finishCapture( Unit & open, Pending & pending,
                                      const core::quantities::Time timeout) -> std::shared_ptr<const Capture>
    {
        const auto deadline = Clock::now()
                            + std::chrono::duration_cast<Clock::duration>( std::chrono::duration<double>( timeout.value()));

        for( ;;)
        {
            bool ready = false;

            check( "IsReady", open.Api->isReady( open.Handle, ready));

            if( ready)
            {
                break;
            }

            if( Clock::now() >= deadline)
            {
                //
                // Stopped on the way out, so the unit does not sit armed for
                // the next script's stimulus -- see safe().
                //
                static_cast<void>( open.Api->stop( open.Handle));

                return nullptr;
            }

            std::this_thread::sleep_for( kPollInterval);
        }

        auto          samples  = static_cast<std::uint32_t>( pending.Shape.samples());
        std::int16_t  overflow = 0;

        check( "GetValues", open.Api->getValues( open.Handle, samples, overflow));

        static_cast<void>( open.Api->stop( open.Handle));

        auto result = std::make_shared<Capture>();

        for( unsigned channel = 1; channel <= channel_count; ++channel)
        {
            if( !pending.Enabled.at( channel - 1))
            {
                continue;
            }

            const auto & scale  = *mApplied.at( channel - 1);
            const auto & buffer = pending.Buffers.at( channel - 1);
            const auto   count  = std::min<std::size_t>( samples, buffer.size());

            //
            // Counts to volts at the probe tip. The ADC saw input + the SDK's
            // offset, scaled to the range: so the input is the scaled count
            // minus that offset, times the probe ratio.
            //
            std::vector<double> volts( count);

            for( std::size_t i = 0; i < count; ++i)
            {
                volts[ i] = ( static_cast<double>( buffer[ i]) / static_cast<double>( open.AdcMax) * scale.Range
                              - scale.PicoOffset) * scale.Attenuation;
            }

            result->Traces.at( channel - 1) = core::Waveform{
                core::quantityKindOf<core::quantities::Voltage>(),
                core::Waveform::Timing{ core::quantities::Time{ pending.Shape.Origin },
                                        core::quantities::Time{ pending.Shape.Interval } },
                std::move( volts) };

            result->Clipped.at( channel - 1) = ( overflow & ( 1 << ( channel - 1))) != 0;
        }

        return result;
    }

    auto Oscilloscope::freshCapture() -> std::shared_ptr<const Capture>
    {
        auto & open = session();

        auto pending = beginCapture( open, false);

        auto result = finishCapture( open, *pending, kDefaultCaptureTimeout);

        if( !result)
        {
            throw io::TransportTimeout( "PicoScope did not complete a free-running capture within "
                                        + describeTime( kDefaultCaptureTimeout.value()));
        }

        return result;
    }

    auto Oscilloscope::capture() -> std::shared_ptr<const Capture>
    {
        if( mHeld)
        {
            return mHeld;
        }

        return isSimulated() ? simulatedCapture() : freshCapture();
    }

    auto Oscilloscope::armSingle( const SingleConfig & config) -> void
    {
        invalidate();

        mPending.reset();
        mArmed = false;

        if( isSimulated())
        {
            mArmed = true;

            return;
        }

        auto & open = session();

        // A capture already running would race the one about to start.
        static_cast<void>( open.Api->stop( open.Handle));

        const auto armTimeout = config.ArmTimeout.value_or( kDefaultArmTimeout);

        auto pending = beginCapture( open, true);

        //
        // The SDK has no "armed and ready" to poll -- the DSOX1202G's :AER? --
        // so ready is the time it takes the unit to take the pre-trigger
        // samples, after which any trigger yields a whole record, plus a
        // margin. A script that causes its event the moment Arm returns is
        // then no more exposed than on the scope that can say so.
        //
        const double fill = static_cast<double>( pending->Shape.Pre) * pending->Shape.Interval;

        if( fill > armTimeout.value())
        {
            static_cast<void>( open.Api->stop( open.Handle));

            throw io::TransportTimeout( "PicoScope needs " + describeTime( fill)
                                        + " to take the pre-trigger samples, longer than the arm timeout of "
                                        + describeTime( armTimeout.value()));
        }

        std::this_thread::sleep_for( std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::duration<double>( fill))
                                     + kArmMargin);

        mPending = std::move( pending);
        mArmed   = true;
    }

    auto Oscilloscope::awaitAcquisition( const SingleConfig & config) -> bool
    {
        if( !mArmed)
        {
            return false;
        }

        mArmed = false;

        if( isSimulated())
        {
            mCompleted = mCaptureCompletes;

            if( mCompleted)
            {
                mHeld = simulatedCapture();
            }

            return mCompleted;
        }

        auto pending = std::move( mPending);

        mPending.reset();

        auto & open = session();

        auto result = finishCapture( open, *pending, config.Timeout.value_or( kDefaultCaptureTimeout));

        mCompleted = result != nullptr;

        if( result)
        {
            mHeld = std::move( result);
        }

        return mCompleted;
    }

    auto Oscilloscope::fetchWaveform( const WaveformConfig & config) -> core::Waveform
    {
        const auto held = capture();

        const auto & trace = held->Traces.at( config.Channel - 1);

        if( trace)
        {
            return *trace;
        }

        if( isSimulated())
        {
            // As the DSOX1202G does: a channel nothing was put on is an empty
            // trace, not an error.
            return core::Waveform{};
        }

        throw NotSetUp( "channel " + std::to_string( config.Channel) + " is not set up (or is switched off), so "
                        "there is no record of it -- Setup( Osc.channel<" + std::to_string( config.Channel) + ">().range( ...))");
    }

    auto Oscilloscope::measureNow( const Mode mode, const unsigned channel,
                                   const std::optional<double> lowThreshold,
                                   const std::optional<double> highThreshold) -> double
    {
        const auto held  = capture();
        const auto & trace = held->Traces.at( channel - 1);

        if( !trace)
        {
            if( isSimulated())
            {
                throw core::UnmeasurableReading( "no waveform on this channel");
            }

            throw NotSetUp( "channel " + std::to_string( channel) + " is not set up (or is switched off) -- "
                            "Setup( Osc.channel<" + std::to_string( channel) + ">().range( ...)) before measuring it");
        }

        //
        // A clipped record is not measured. Its maximum is the range, not the
        // signal, and a Verify of that against a criterion would pass or fail
        // for a reason the report cannot show.
        //
        if( held->Clipped.at( channel - 1))
        {
            throw core::UnmeasurableReading( "the input overflowed the selected range, so the record is clipped"
                                             " -- use a larger range");
        }

        const auto samples  = trace->samples();
        const auto interval = trace->timing().Increment.value();
        const auto low      = lowThreshold.value_or( 0.1);
        const auto high     = highThreshold.value_or( 0.9);

        measure::Result result;

        switch( mode)
        {
            case Mode::Vpp:           result = measure::vpp( samples);                       break;
            case Mode::Vmax:          result = measure::vmax( samples);                      break;
            case Mode::Vmin:          result = measure::vmin( samples);                      break;
            case Mode::Vrms:          result = measure::vrms( samples);                      break;
            case Mode::Vaverage:      result = measure::vaverage( samples);                  break;
            case Mode::Vbase:         result = measure::vbase( samples);                     break;
            case Mode::Vtop:          result = measure::vtop( samples);                      break;
            case Mode::Vamplitude:    result = measure::vamplitude( samples);                break;
            case Mode::Frequency:     result = measure::frequency( samples, interval);       break;
            case Mode::Period:        result = measure::period( samples, interval);          break;
            case Mode::RiseTime:      result = measure::riseTime( samples, interval, low, high); break;
            case Mode::FallTime:      result = measure::fallTime( samples, interval, low, high); break;
            case Mode::PositiveWidth: result = measure::positiveWidth( samples, interval);   break;
            case Mode::NegativeWidth: result = measure::negativeWidth( samples, interval);   break;
        }

        if( !result)
        {
            throw core::UnmeasurableReading( result.error());
        }

        return *result;
    }
} // namespace hal::pico_5244d
