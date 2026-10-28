#include "hal/pico_5244d.hpp"

//
// Everything the scope's signal generator puts through the SDK: one call,
// ps5000aSetSigGenBuiltInV2, which sets the whole waveform at once. See the
// header's comment on WaveformGenerator for what is not yet confirmed.
//
namespace hal::pico_5244d::detail
{
    auto program( Unit & unit, const GeneratorProgram & waveform) -> void
    {
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
        program( unit, GeneratorProgram{ 8, 0, 0.0, 0, 0 });
    }
} // namespace hal::pico_5244d::detail

namespace hal::pico_5244d
{
    auto WaveformGenerator::identity() -> std::string
    {
        return detail::identityOf( detail::openedUnit( mConnection, std::nullopt));
    }
} // namespace hal::pico_5244d
