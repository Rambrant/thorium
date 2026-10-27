#pragma once

#include <optional>

#include "core/quantities/quantity.hpp"

#include "hal/driver/builder.hpp"

#include "hal/pico_5244d/vocabulary.hpp"

//
// The single-shot capture, for Arm and Await. Part of hal/pico_5244d.hpp.
//
// Timeout is how long Await waits for a trigger. ArmTimeout is the longest
// Arm will take to be armed, and on this instrument it bounds something
// different from the DSOX1202G's: a PicoScope has no "armed and ready"
// register, so Arm waits out the pre-trigger fill -- the time the unit needs
// before a trigger can possibly produce a complete record -- and refuses (by
// throwing, as the DSOX1202G does when it never arms) if that exceeds this.
//
namespace hal::pico_5244d
{
    struct SingleConfig
    {
        Oscilloscope &                              Instrument;

        std::optional<core::quantities::Time>       Timeout{};
        std::optional<core::quantities::Time>       ArmTimeout{};
    };

    class SingleBuilder : public ConfigBuilder<SingleBuilder, SingleConfig>
    {
        public:
            using Config = SingleConfig;

            explicit SingleBuilder( Oscilloscope & instrument) : ConfigBuilder( Config{ instrument }) {}

            [[nodiscard]] auto timeout( const core::quantities::Time value) const    { return with( &Config::Timeout, value); }
            [[nodiscard]] auto armTimeout( const core::quantities::Time value) const { return with( &Config::ArmTimeout, value); }
    };
} // namespace hal::pico_5244d
