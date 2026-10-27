#pragma once

#include <optional>

#include "core/quantities/quantity.hpp"

#include "hal/driver/builder.hpp"

#include "hal/pico_5244d/vocabulary.hpp"

//
// :CHANnel<N>. Part of hal/pico_5244d.hpp.
//
// range() where the DSOX1202G has voltsPerDivision(). A PicoScope has no
// divisions: its vertical setting is a discrete full-scale range (+/-10 mV to
// +/-20 V in a 1-2-5 sequence), and the honest way to ask for one is to say
// the largest voltage the signal reaches. The driver picks the smallest range
// that holds it, at the probe tip, and a request above +/-20 V (times the probe
// ratio) is a SettingOutOfRange -- not a silent clamp to a range the signal
// would overflow.
//
// The offset is the DSOX1202G's meaning too: the level shown at the centre of
// the range, at the probe tip. (The SDK's own analog offset is the opposite
// sign -- a voltage *added* to the input -- and is converted in the driver.)
//
namespace hal::pico_5244d
{
    struct ChannelConfig
    {
        Oscilloscope &                              Instrument;

        unsigned                                    Channel;

        std::optional<Coupling>                     InputCoupling{};
        std::optional<core::quantities::Voltage>    Range{};
        std::optional<core::quantities::Voltage>    VerticalOffset{};
        std::optional<Bandwidth>                    BandwidthLimit{};
        std::optional<double>                       ProbeAttenuation{};
        std::optional<ChannelDisplay>               Display{};
    };

    class ChannelBuilder : public ConfigBuilder<ChannelBuilder, ChannelConfig>
    {
        public:
            using Config = ChannelConfig;

            ChannelBuilder( Oscilloscope & instrument, const unsigned channel) :
                ConfigBuilder( Config{ instrument, channel })
            {}

            [[nodiscard]] auto coupling( const Coupling value) const                   { return with( &Config::InputCoupling, value); }
            [[nodiscard]] auto range( const core::quantities::Voltage value) const     { return with( &Config::Range, value); }
            [[nodiscard]] auto verticalOffset( const core::quantities::Voltage value) const { return with( &Config::VerticalOffset, value); }
            [[nodiscard]] auto bandwidth( const Bandwidth value) const                 { return with( &Config::BandwidthLimit, value); }
            [[nodiscard]] auto probeAttenuation( const double ratio) const             { return with( &Config::ProbeAttenuation, ratio); }
            [[nodiscard]] auto display( const ChannelDisplay value) const              { return with( &Config::Display, value); }
    };
} // namespace hal::pico_5244d
