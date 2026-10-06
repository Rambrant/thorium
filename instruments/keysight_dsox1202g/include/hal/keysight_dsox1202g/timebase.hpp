#pragma once

#include <optional>

#include "core/quantities/quantity.hpp"

#include "hal/driver/builder.hpp"

#include "hal/keysight_dsox1202g/vocabulary.hpp"

//
// :TIMebase -- TimebaseConfig and TimebaseBuilder.
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
    // :TIMebase -- how much time the record covers, and where the trigger sits
    // within it.
    //
    struct TimebaseConfig
    {
        DSOX1202G &                                 Instrument;

        //
        // :TIMebase:SCALe, seconds per division -- the number written on the
        // front panel and in every test spec, not :TIMebase:RANGe, which is
        // the same fact times ten. Both exist on the instrument; carrying both
        // here would have let a script set them to values that disagree.
        //
        std::optional<core::quantities::Time>       TimePerDivision{};

        //
        // :TIMebase:POSition -- time between the trigger event and the
        // reference point below. Positive delays the record after the trigger;
        // negative shows what preceded it.
        //
        std::optional<core::quantities::Time>       Position{};
        std::optional<TimebaseReference>            Reference{};
    };

    class TimebaseBuilder : public ConfigBuilder<TimebaseBuilder, TimebaseConfig>
    {
        public:
            using Config = TimebaseConfig;

            explicit TimebaseBuilder( DSOX1202G & instrument) : ConfigBuilder( Config{ instrument }) {}

            [[nodiscard]]
            auto timePerDivision( const core::quantities::Time value) const
            {
                return with( &Config::TimePerDivision, value);
            }

            [[nodiscard]]
            auto position( const core::quantities::Time value) const
            {
                return with( &Config::Position, value);
            }

            [[nodiscard]]
            auto reference( const TimebaseReference value) const
            {
                return with( &Config::Reference, value);
            }

    };
} // namespace hal::keysight_dsox1202g
