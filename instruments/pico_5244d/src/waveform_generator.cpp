#include <cmath>

#include "hal/pico_5244d.hpp"

//
// Everything the scope's signal generator puts through the SDK: one call,
// ps5000aSetSigGenBuiltInV2, which sets the whole waveform at once. See the
// header's comment on WaveformGenerator for what is not yet confirmed.
//
namespace hal::pico_5244d::detail
{
    auto drawPeriod( const std::int32_t drawn, const std::int16_t low, const std::int16_t high,
                     const std::size_t count) -> std::vector<std::int16_t>
    {
        std::vector<std::int16_t> samples( count);

        const auto span = static_cast<double>( high) - static_cast<double>( low);

        for( std::size_t i = 0; i < count; ++i)
        {
            double level = 0.0;

            if( drawn == 1)
            {
                const auto t = static_cast<double>( i) / static_cast<double>( count);
                level = t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t;
            }
            else
            {
                const auto t = static_cast<double>( i) / static_cast<double>( count - 1);
                level = drawn == 2 ? t : 1.0 - t;
            }

            samples[ i] = static_cast<std::int16_t>( std::lround( static_cast<double>( low) + span * level));
        }

        return samples;
    }

    //
    // The built-in generator plays a triangle and the ramps at half the
    // frequency it is asked for, so those are drawn here instead and handed to
    // the arbitrary waveform generator, whose frequency is a phase step.
    //
    static auto programDrawn( Unit & unit, const GeneratorProgram & waveform) -> void
    {
        Ps5000a::ArbitraryLimits limits;

        check( "SigGenArbitraryMinMaxValues", unit.Api->arbitraryLimits( unit.Handle, limits));

        auto samples = drawPeriod( waveform.Drawn, limits.MinValue, limits.MaxValue, limits.MaxSize);

        std::uint32_t phase = 0;

        check( "SigGenFrequencyToPhase",
               unit.Api->frequencyToPhase( unit.Handle, waveform.Hertz, limits.MaxSize, phase));

        check( "SetSigGenArbitrary",
               unit.Api->setSigGenArbitrary( unit.Handle, waveform.OffsetMicrovolts, waveform.PeakToPeakMicrovolts,
                                             phase, samples.data(), static_cast<std::int32_t>( samples.size())));
    }

    auto program( Unit & unit, const GeneratorProgram & waveform) -> void
    {
        if( waveform.Drawn != 0)
        {
            programDrawn( unit, waveform);
            return;
        }

        check( "SetSigGenBuiltInV2",
               unit.Api->setSigGen( unit.Handle, waveform.OffsetMicrovolts, waveform.PeakToPeakMicrovolts,
                                    waveform.WaveType, waveform.Operation, waveform.Hertz));
    }

    //
    // DC at zero volts: the one waveform that is the same whatever was playing,
    // and that no frequency or amplitude limit can refuse. The SDK has no
    // "output off" -- this is what its own examples do.
    //
    auto silence( Unit & unit) -> void
    {
        program( unit, GeneratorProgram{ 8, 0, 0, 0.0, 0, 0 });
    }
} // namespace hal::pico_5244d::detail

namespace hal::pico_5244d
{
    auto WaveformGenerator::identity() -> std::string
    {
        return detail::identityOf( detail::openedUnit( mConnection, std::nullopt));
    }
} // namespace hal::pico_5244d
