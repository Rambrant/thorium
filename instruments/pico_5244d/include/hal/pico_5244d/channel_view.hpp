#pragma once

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/pico_5244d/channel.hpp"
#include "hal/pico_5244d/fetch.hpp"
#include "hal/pico_5244d_oscilloscope.hpp"

//
// Osc.channel<N>(): the object a script's Setup and Measure calls go through.
// Part of hal/pico_5244d.hpp.
//
// The same shape as hal::keysight_dsox1202g::Channel, down to the sharp edge
// its README documents: a Port binds to the instrument, not to this temporary
// view, and reads the channel and mode current when rawMeasure() runs.
//
namespace hal::pico_5244d
{
    template<unsigned N>
        requires ValidChannel<N>
    class Channel
    {
        public:
            static constexpr unsigned Number = N;

            explicit Channel( Oscilloscope & instrument) : mInstrument( instrument) {}

            [[nodiscard]] auto coupling( const Coupling value) const -> ChannelBuilder
            {
                return ChannelBuilder{ mInstrument, N }.coupling( value);
            }

            [[nodiscard]] auto range( const core::quantities::Voltage value) const -> ChannelBuilder
            {
                return ChannelBuilder{ mInstrument, N }.range( value);
            }

            [[nodiscard]] auto verticalOffset( const core::quantities::Voltage value) const -> ChannelBuilder
            {
                return ChannelBuilder{ mInstrument, N }.verticalOffset( value);
            }

            [[nodiscard]] auto bandwidth( const Bandwidth value) const -> ChannelBuilder
            {
                return ChannelBuilder{ mInstrument, N }.bandwidth( value);
            }

            [[nodiscard]] auto probeAttenuation( const double ratio) const -> ChannelBuilder
            {
                return ChannelBuilder{ mInstrument, N }.probeAttenuation( ratio);
            }

            [[nodiscard]] auto display( const ChannelDisplay value) const -> ChannelBuilder
            {
                return ChannelBuilder{ mInstrument, N }.display( value);
            }

            [[nodiscard]] auto waveform() const -> WaveformBuilder
            {
                return WaveformBuilder{ mInstrument, N };
            }

            [[nodiscard]] auto vpp()           -> core::Port<core::quantities::Voltage, Oscilloscope>   { return port<core::quantities::Voltage>( Oscilloscope::Mode::Vpp, "Vpp"); }
            [[nodiscard]] auto vmax()          -> core::Port<core::quantities::Voltage, Oscilloscope>   { return port<core::quantities::Voltage>( Oscilloscope::Mode::Vmax, "Vmax"); }
            [[nodiscard]] auto vmin()          -> core::Port<core::quantities::Voltage, Oscilloscope>   { return port<core::quantities::Voltage>( Oscilloscope::Mode::Vmin, "Vmin"); }
            [[nodiscard]] auto vrms()          -> core::Port<core::quantities::Voltage, Oscilloscope>   { return port<core::quantities::Voltage>( Oscilloscope::Mode::Vrms, "Vrms"); }
            [[nodiscard]] auto vaverage()      -> core::Port<core::quantities::Voltage, Oscilloscope>   { return port<core::quantities::Voltage>( Oscilloscope::Mode::Vaverage, "Vaverage"); }
            [[nodiscard]] auto vbase()         -> core::Port<core::quantities::Voltage, Oscilloscope>   { return port<core::quantities::Voltage>( Oscilloscope::Mode::Vbase, "Vbase"); }
            [[nodiscard]] auto vtop()          -> core::Port<core::quantities::Voltage, Oscilloscope>   { return port<core::quantities::Voltage>( Oscilloscope::Mode::Vtop, "Vtop"); }
            [[nodiscard]] auto vamplitude()    -> core::Port<core::quantities::Voltage, Oscilloscope>   { return port<core::quantities::Voltage>( Oscilloscope::Mode::Vamplitude, "Vamplitude"); }
            [[nodiscard]] auto frequency()     -> core::Port<core::quantities::Frequency, Oscilloscope> { return port<core::quantities::Frequency>( Oscilloscope::Mode::Frequency, "Frequency"); }
            [[nodiscard]] auto period()        -> core::Port<core::quantities::Time, Oscilloscope>      { return port<core::quantities::Time>( Oscilloscope::Mode::Period, "Period"); }
            [[nodiscard]] auto positiveWidth() -> core::Port<core::quantities::Time, Oscilloscope>      { return port<core::quantities::Time>( Oscilloscope::Mode::PositiveWidth, "PositiveWidth"); }
            [[nodiscard]] auto negativeWidth() -> core::Port<core::quantities::Time, Oscilloscope>      { return port<core::quantities::Time>( Oscilloscope::Mode::NegativeWidth, "NegativeWidth"); }

            //
            // 10 % / 90 % up front, as the DSOX1202G's are: the thresholds are
            // part of what a rise time means, and these are what its firmware
            // measures by default, so a script moves between the two unchanged.
            //
            [[nodiscard]]
            auto riseTime() -> core::Port<core::quantities::Time, Oscilloscope>
            {
                return port<core::quantities::Time>( Oscilloscope::Mode::RiseTime, "RiseTime").lowThreshold( 0.1).highThreshold( 0.9);
            }

            [[nodiscard]]
            auto fallTime() -> core::Port<core::quantities::Time, Oscilloscope>
            {
                return port<core::quantities::Time>( Oscilloscope::Mode::FallTime, "FallTime").lowThreshold( 0.1).highThreshold( 0.9);
            }

        private:
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto port( const Oscilloscope::Mode mode, const std::string_view name) -> core::Port<QuantityT, Oscilloscope>
            {
                mInstrument.setChannel( N);
                mInstrument.setMode( mode);

                return core::Port<QuantityT, Oscilloscope>{ mInstrument }.qualifiedBy( name);
            }

            Oscilloscope & mInstrument;
    };
} // namespace hal::pico_5244d
