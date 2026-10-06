#pragma once

#include <optional>

#include "hal/driver/builder.hpp"

#include "hal/keysight_dsox1202g/vocabulary.hpp"

//
// :WAVeform -- WaveformConfig and WaveformBuilder, for Fetch.
//
// The config is what a verb receives and the builder is how a script fills
// it in, so the two live together: adding a setting is a field here, a setter
// here, a line where DSOX1202G sends it (src/keysight_dsox1202g.cpp)
// and a fragment in its describeConfig (customization.hpp).
//
// Part of hal/keysight_dsox1202g.hpp -- include that, not this.
//

namespace hal::keysight_dsox1202g
{
    //
    // :WAVeform -- which record to transfer off the instrument, for Fetch.
    //
    // Carries the channel and nothing else, as the Infiniium driver's does,
    // and for mostly the same reason: the format is fixed by this driver (the
    // samples come back scaled into volts, see fetchWaveform) and how many
    // points there are and how far apart they sit were settled when the scope
    // triggered.
    //
    // "Mostly", because this instrument does have one transfer setting worth a
    // name eventually: :WAVeform:POINts and :WAVeform:POINts:MODE decide how
    // much of the acquisition record is sent -- a decimated screenful or the
    // whole raw memory. That is a genuine choice and it is still not modelled:
    // what comes back is the screen record this instrument defaults to, which
    // is what this rig captures. It is the first thing this builder should
    // grow, and it arrives together with binary transfer rather than before it
    // -- see fetchWaveform() on why the ASCII format that makes the transfer
    // possible today is also what makes a deep record expensive.
    //
    struct WaveformConfig
    {
        DSOX1202G &  Instrument;

        //
        // Not optional, for ChannelConfig::Channel's reason: a transfer with
        // no source is not an underspecified instruction, it is not one at
        // all.
        //
        unsigned     Channel;
    };

    class WaveformBuilder : public ConfigBuilder<WaveformBuilder, WaveformConfig>
    {
        public:
            using Config = WaveformConfig;

            WaveformBuilder( DSOX1202G & instrument, const unsigned channel) :
                ConfigBuilder( Config{ instrument, channel })
            {}

            //
            // No setters yet -- see WaveformConfig on the one this instrument
            // has and why it waits for the transport. It is still a builder
            // rather than a bare config so that Fetch's argument reads like
            // every other verb's, and so that :WAVeform:POINts arrives as a
            // method rather than as a new type.
            //
    };
} // namespace hal::keysight_dsox1202g
