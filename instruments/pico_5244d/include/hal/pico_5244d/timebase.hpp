#pragma once

#include <optional>

#include "core/quantities/quantity.hpp"

#include "hal/driver/builder.hpp"

#include "hal/pico_5244d/vocabulary.hpp"

//
// :TIMebase. Part of hal/pico_5244d.hpp.
//
// The DSOX1202G's three settings plus one the PicoScope needs: a record has to
// say how many samples it wants, because a digitiser has no screen to default
// that from. The window is ten divisions, as on the DSOX1202G, so
// timePerDivision means the same on both.
//
// Reference and position place the *trigger* in the record exactly as the
// DSOX1202G does: the reference is where on the window the position is
// measured from, the position is the time from the trigger to that reference
// point. Left, 0 s is therefore a record that starts at the trigger; Center, 0
// s puts the trigger in the middle.
//
// None of it is sent until a capture starts, when the SDK needs a timebase
// number and sample counts -- see Oscilloscope::configureTimebase for what is
// checked up front.
//
namespace hal::pico_5244d
{
    struct TimebaseConfig
    {
        Oscilloscope &                              Instrument;

        std::optional<core::quantities::Time>       TimePerDivision{};
        std::optional<core::quantities::Time>       Position{};
        std::optional<TimebaseReference>            Reference{};
        std::optional<unsigned>                     Points{};
    };

    class TimebaseBuilder : public ConfigBuilder<TimebaseBuilder, TimebaseConfig>
    {
        public:
            using Config = TimebaseConfig;

            explicit TimebaseBuilder( Oscilloscope & instrument) : ConfigBuilder( Config{ instrument }) {}

            [[nodiscard]] auto timePerDivision( const core::quantities::Time value) const { return with( &Config::TimePerDivision, value); }
            [[nodiscard]] auto position( const core::quantities::Time value) const       { return with( &Config::Position, value); }
            [[nodiscard]] auto reference( const TimebaseReference value) const             { return with( &Config::Reference, value); }

            //
            // The samples the window should hold. The sample interval is the
            // finest the instrument offers that does not exceed window/points,
            // so this is "at least this many" in effect.
            //
            [[nodiscard]] auto points( const unsigned value) const                         { return with( &Config::Points, value); }
    };
} // namespace hal::pico_5244d
