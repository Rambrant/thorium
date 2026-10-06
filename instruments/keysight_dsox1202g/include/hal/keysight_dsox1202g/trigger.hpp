#pragma once

#include <optional>

#include "core/quantities/quantity.hpp"

#include "hal/driver/builder.hpp"

#include "hal/keysight_dsox1202g/vocabulary.hpp"

//
// :TRIGger -- TriggerConfig and TriggerBuilder.
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
    // :TRIGger -- when to capture.
    //
    // Source, slope and level travel together because on the instrument they
    // are not independent: the slope applies to whichever source
    // :TRIGger[:EDGE]:SOURce last selected, and the level is stored per
    // channel.
    //
    struct TriggerConfig
    {
        DSOX1202G &                                 Instrument;

        //
        // Which channel the edge trigger watches. A plain unsigned here,
        // although the builder that sets it takes the channel number as a
        // template argument checked against ValidChannel -- the check happens
        // where the number is written, and what survives into the config is
        // the value.
        //
        // Channels only, deliberately. This instrument's edge trigger also
        // accepts EXTernal (the Ext Trig BNC), LINE (the mains) and WGEN (its
        // own generator's sync), and none of the three is wired on this rig:
        // rig/wiring.inc routes signals to the front BNCs and nothing else.
        // Modelling a source no bench here can reach would mean maintaining a
        // setting nothing can exercise -- the same argument the Infiniium
        // driver makes for leaving the non-edge trigger kinds out. The day a
        // rig cables the Ext Trig input, this field grows a source enum and
        // the builder grows externalSource().
        //
        std::optional<unsigned>                     EdgeSource{};
        std::optional<TriggerSlope>                 Slope{};
        std::optional<core::quantities::Voltage>    Level{};
        std::optional<TriggerSweep>                 Sweep{};
        std::optional<TriggerCoupling>              Coupling{};

        //
        // The reject filters, which are their own command here rather than two
        // more couplings -- see TriggerReject.
        //
        std::optional<TriggerReject>                Reject{};

        //
        // :TRIGger:HOLDoff, 60 ns to 10 s on this instrument.
        //
        std::optional<core::quantities::Time>       Holdoff{};
    };

    class TriggerBuilder : public ConfigBuilder<TriggerBuilder, TriggerConfig>
    {
        public:
            using Config = TriggerConfig;

            explicit TriggerBuilder( DSOX1202G & instrument) : ConfigBuilder( Config{ instrument }) {}

            //
            // Which channel the trigger watches, checked the same way
            // DSOX1202G::channel<N>() is -- edgeSource<3>() has no valid
            // instantiation on a two-channel scope, rather than being a
            // settings-conflict error discovered on the bench.
            //
            template<unsigned N>
                requires ValidChannel<N>
            [[nodiscard]]
            auto edgeSource() const
            {
                return with( &Config::EdgeSource, N);
            }

            [[nodiscard]]
            auto slope( const TriggerSlope value) const
            {
                return with( &Config::Slope, value);
            }

            [[nodiscard]]
            auto level( const core::quantities::Voltage value) const
            {
                return with( &Config::Level, value);
            }

            [[nodiscard]]
            auto sweep( const TriggerSweep value) const
            {
                return with( &Config::Sweep, value);
            }

            [[nodiscard]]
            auto coupling( const TriggerCoupling value) const
            {
                return with( &Config::Coupling, value);
            }

            [[nodiscard]]
            auto reject( const TriggerReject value) const
            {
                return with( &Config::Reject, value);
            }

            [[nodiscard]]
            auto holdoff( const core::quantities::Time value) const
            {
                return with( &Config::Holdoff, value);
            }

    };
} // namespace hal::keysight_dsox1202g
