#pragma once

#include <optional>

#include "core/quantities/quantity.hpp"

#include "hal/driver/builder.hpp"

#include "hal/pico_5244d/vocabulary.hpp"

//
// :TRIGger, as far as a PicoScope has one: an edge on a channel at a level.
// Part of hal/pico_5244d.hpp.
//
// Four settings, against the DSOX1202G's seven. The 5244D's trigger is a
// comparator on the digitised signal -- there is no coupling, no reject filter
// and no holdoff to set, and a builder offering them would be accepting
// settings it then ignored. The level is a Voltage at the probe tip.
//
// Nothing is sent when this is applied: the SDK takes the trigger as part of
// starting a capture (ps5000aSetSimpleTrigger, called with the run), so a
// Setup here records what the next Arm or Measure will use.
//
namespace hal::pico_5244d
{
    struct TriggerConfig
    {
        Oscilloscope &                              Instrument;

        std::optional<unsigned>                     EdgeSource{};
        std::optional<TriggerSlope>                 Slope{};
        std::optional<core::quantities::Voltage>    Level{};
        std::optional<TriggerSweep>                 Sweep{};
    };

    class TriggerBuilder : public ConfigBuilder<TriggerBuilder, TriggerConfig>
    {
        public:
            using Config = TriggerConfig;

            explicit TriggerBuilder( Oscilloscope & instrument) : ConfigBuilder( Config{ instrument }) {}

            template<unsigned N>
                requires ValidChannel<N>
            [[nodiscard]]
            auto edgeSource() const
            {
                return with( &Config::EdgeSource, N);
            }

            [[nodiscard]] auto slope( const TriggerSlope value) const                { return with( &Config::Slope, value); }
            [[nodiscard]] auto level( const core::quantities::Voltage value) const   { return with( &Config::Level, value); }

            //
            // Auto triggers by itself after kAutoTriggerMs if nothing crosses
            // the level; Normal waits for ever (until Await's timeout).
            //
            [[nodiscard]] auto sweep( const TriggerSweep value) const                { return with( &Config::Sweep, value); }
    };
} // namespace hal::pico_5244d
