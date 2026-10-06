#pragma once

#include <optional>

#include "core/quantities/quantity.hpp"

#include "hal/driver/builder.hpp"

#include "hal/keysight_dsox1202g/vocabulary.hpp"

//
// :CHANnel<N> -- ChannelConfig and ChannelBuilder, the setting side of one input.
// The channel view a script names, Channel<N>, is in channel_view.hpp.
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
    // :CHANnel<N> -- one input, vertically.
    //
    struct ChannelConfig
    {
        DSOX1202G &                                 Instrument;

        //
        // Which input this configures. Not optional, unlike everything below
        // it: a channel config with no channel is not an underspecified
        // instruction, it is not an instruction at all. Filled in by
        // Channel<N>, so a script can only ever produce one of these by naming
        // a channel that ValidChannel accepts.
        //
        unsigned                                    Channel;

        //
        // Named InputCoupling rather than Coupling, which is what it renders
        // as and what the builder method is called: a member whose name is
        // also the name of the type used to declare it changes the meaning of
        // that name inside the class, and is ill-formed. The trigger's own
        // coupling field escapes this only because its enum is TriggerCoupling.
        //
        std::optional<Coupling>                     InputCoupling{};

        //
        // :CHANnel<N>:SCALe -- volts per division, the front-panel number
        // again, and again not its :RANGe sibling for the reason
        // TimebaseConfig gives about the horizontal axis.
        //
        std::optional<core::quantities::Voltage>    VoltsPerDivision{};

        //
        // :CHANnel<N>:OFFSet -- the voltage represented at the centre of the
        // screen. Offsetting a small signal that sits on a large DC level is
        // what lets the vertical scale be turned up far enough to resolve it,
        // and every measurement the scope then makes is relative to the trace,
        // so the offset does not bias the answer.
        //
        std::optional<core::quantities::Voltage>    VerticalOffset{};

        std::optional<Bandwidth>                    BandwidthLimit{};

        //
        // :CHANnel<N>:PROBe -- the probe's attenuation ratio, and a plain
        // number rather than an enum.
        //
        // The Infiniium driver's ProbeAdapter names four external divider
        // adapters because that is what that command takes: an adapter, from a
        // fixed list, valid only against particular probe models. This command
        // takes a ratio -- "the probe attenuation factor may be 0.1 to 10000"
        // -- and 10 here means a 10:1 probe whoever made it. An enum would
        // have had to invent a name per ratio and would still not cover the
        // range.
        //
        // A double, therefore, and the range is not enforced: this driver
        // cannot know what is clipped to the end of the probe, so a ratio
        // outside the instrument's range is a settings-conflict error on the
        // bench rather than a compile error here. Same line the Infiniium
        // driver draws around its own probe setting, and the same reason:
        // which probe is fitted is a bench fact, where the channel count is a
        // model fact.
        //
        // Note what this does NOT do: change what the input can survive. The
        // command scales the display, the measurements and the trigger levels
        // -- it does not attenuate anything. Telling the scope about a divider
        // that is not there produces readings ten times too large, with
        // nothing anywhere to catch it.
        //
        std::optional<double>                       ProbeAttenuation{};
        std::optional<ChannelDisplay>               Display{};
    };

    //
    // The per-channel chain.
    //
    // Note what it holds: the instrument and a channel *number*, never a
    // Channel<N>. That is the dangling-reference lesson core::Port learned
    // (see Channel's own comment in channel_view.hpp) -- `Osc1.channel<2>().coupling( ... )`
    // produces a temporary channel view which is gone by the end of the full
    // expression, well before Setup gets its hands on the config.
    //
    class ChannelBuilder : public ConfigBuilder<ChannelBuilder, ChannelConfig>
    {
        public:
            using Config = ChannelConfig;

            ChannelBuilder( DSOX1202G & instrument, const unsigned channel) :
                ConfigBuilder( Config{ instrument, channel })
            {}

            [[nodiscard]]
            auto coupling( const Coupling value) const
            {
                return with( &Config::InputCoupling, value);
            }

            [[nodiscard]]
            auto voltsPerDivision( const core::quantities::Voltage value) const
            {
                return with( &Config::VoltsPerDivision, value);
            }

            [[nodiscard]]
            auto verticalOffset( const core::quantities::Voltage value) const
            {
                return with( &Config::VerticalOffset, value);
            }

            [[nodiscard]]
            auto bandwidth( const Bandwidth value) const
            {
                return with( &Config::BandwidthLimit, value);
            }

            //
            // The probe's divider ratio: 10.0 for an ordinary 10:1 probe, 1.0
            // for a direct BNC lead. See ChannelConfig::ProbeAttenuation on
            // why this is a number rather than one of a fixed set of adapters,
            // and on what it does and does not change.
            //
            [[nodiscard]]
            auto probeAttenuation( const double ratio) const
            {
                return with( &Config::ProbeAttenuation, ratio);
            }

            [[nodiscard]]
            auto display( const ChannelDisplay value) const
            {
                return with( &Config::Display, value);
            }

    };
} // namespace hal::keysight_dsox1202g
