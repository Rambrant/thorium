#pragma once

#include "hal/driver/builder.hpp"

#include "hal/pico_5244d/vocabulary.hpp"

//
// Fetch( Osc.channel<N>().waveform()): the record off one channel. Part of
// hal/pico_5244d.hpp.
//
namespace hal::pico_5244d
{
    struct WaveformConfig
    {
        Oscilloscope &  Instrument;

        unsigned        Channel;
    };

    class WaveformBuilder : public ConfigBuilder<WaveformBuilder, WaveformConfig>
    {
        public:
            using Config = WaveformConfig;

            WaveformBuilder( Oscilloscope & instrument, const unsigned channel) :
                ConfigBuilder( Config{ instrument, channel })
            {}
    };
} // namespace hal::pico_5244d
