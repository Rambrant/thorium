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

            if( drawn == 1) // Triangle Wave (Perfect 1 Cycle)
            {
                const auto t = static_cast<double>( i) / static_cast<double>( count - 1);
                level = t < 0.5 ? 2.0 * t : 2.0 - (2.0 * t);
            }
            else     // Ramps (Perfect 1 Cycle)
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

        // 1. HARDCODE THE DYNAMIC HARDWARE CLOCKS
        // For PicoScope 5244, the AWG clock is 20 MHz in 8-bit mode, or 5 MHz in high-resolution modes.
        // We can infer the active clock by analyzing which one safely fits inside the MaxSize hardware pool.
        double calculatedAwgClock = 20000000.0; // Assume 20 MHz default
        double initialIdealCount  = calculatedAwgClock / waveform.Hertz;

        // If the count exceeds the scope's max hardware buffer, the hardware has stepped down 
        // its internal clock to 5 MHz (High-Resolution tracking mode).
        if( initialIdealCount > static_cast<double>( limits.MaxSize)) 
        {
            calculatedAwgClock = 5000000.0; // Step down to 5 MHz
        }

        // 2. COMPUTE AND CHUNK-ALIGN THE BUFFER LENGTH
        double idealCount = calculatedAwgClock / waveform.Hertz;
        std::size_t dynamicCount = static_cast<std::size_t>(std::lround(idealCount));

        // Align to strict 8-word chunks to prevent trailing-edge truncation/Missing Edge errors
        dynamicCount = ((dynamicCount + 7) / 8) * 8;

        // CRITICAL FIX: Add an 8-sample memory buffer gap.
        // If the count hits MaxSize exactly, the driver truncates trailing samples.
        // Leaving a small gap ensures every single index point is cleanly evaluated.
        const std::size_t safeMaxSize = (static_cast<std::size_t>(limits.MaxSize) / 8) * 8 - 8;

        if (dynamicCount > safeMaxSize) {
            dynamicCount = safeMaxSize;
        }
        if (dynamicCount < static_cast<std::size_t>(limits.MinSize)) {
            dynamicCount = ((static_cast<std::size_t>(limits.MinSize) + 7) / 8) * 8;
        }


        // 3. Draw the single-pass loop pattern using the stabilized length
        auto samples = drawPeriod( waveform.Drawn, limits.MinValue, limits.MaxValue, dynamicCount);

        std::uint32_t phase = 0;

        // 2. FIX: Compensate for the PicoSDK user-array phase calculation scaling shift.
        // Divide by 2.0 to counteract the double-speed calculation artifact.
        double adjustedHertz = waveform.Hertz / 2.0;

        // Pass safeCount instead of limits.MaxSize so the phase step calculation 
        // cleanly divides into the driver's internal accumulator binary boundaries.
        check( "SigGenFrequencyToPhase",
            unit.Api->frequencyToPhase( unit.Handle, adjustedHertz, static_cast<std::uint32_t>( dynamicCount), phase));

        check( "SetSigGenArbitrary",
            unit.Api->setSigGenArbitrary( unit.Handle, waveform.OffsetMicrovolts, waveform.PeakToPeakMicrovolts,
                                            phase, samples.data(), static_cast<std::int32_t>( samples.size())));

        unit.Api->sigGenSoftwareControl( unit.Handle, 0);
        check( "SigGenSoftwareControl", unit.Api->sigGenSoftwareControl( unit.Handle, 1));
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
