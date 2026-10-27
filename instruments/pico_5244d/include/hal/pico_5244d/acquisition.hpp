#pragma once

#include <optional>

#include "hal/driver/builder.hpp"

#include "hal/pico_5244d/vocabulary.hpp"

//
// The one acquisition setting a PicoScope block capture has that matters here:
// how many bits the ADC resolves. Part of hal/pico_5244d.hpp.
//
// Not the DSOX1202G's `type()`. Normal/averaged/high-resolution/peak-detect are
// acquisition *modes* of a scope whose firmware post-processes; here the
// closest thing to high-resolution is the 12 to 16 bit ADC modes, which trade
// sample rate and channels for bits. Changing it re-applies every channel and
// invalidates any capture held (the unit's whole configuration is reset by the
// SDK on a resolution change).
//
namespace hal::pico_5244d
{
    struct AcquisitionConfig
    {
        Oscilloscope &                  Instrument;

        std::optional<Resolution>       Bits{};
    };

    class AcquisitionBuilder : public ConfigBuilder<AcquisitionBuilder, AcquisitionConfig>
    {
        public:
            using Config = AcquisitionConfig;

            explicit AcquisitionBuilder( Oscilloscope & instrument) : ConfigBuilder( Config{ instrument }) {}

            [[nodiscard]] auto resolution( const Resolution value) const { return with( &Config::Bits, value); }
    };
} // namespace hal::pico_5244d
