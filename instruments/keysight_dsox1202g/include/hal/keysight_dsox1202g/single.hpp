#pragma once

#include <optional>

#include "core/quantities/quantity.hpp"

#include "hal/driver/builder.hpp"

#include "hal/keysight_dsox1202g/vocabulary.hpp"

//
// :SINGle -- SingleConfig and SingleBuilder, for Arm and Await.
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
    // :SINGle, plus the two registers that make it usable -- the config behind
    // Arm and Await (see core/verbs/acquire.hpp).
    //
    // Both verbs take the same config because they are two halves of one
    // operation, and the timeouts belong to the halves rather than to the
    // instrument: a script that arms, drops a rail and waits is stating how
    // long each of those is allowed to take, and those are different numbers
    // for different tests on the same scope.
    //
    struct SingleConfig
    {
        DSOX1202G &                                 Instrument;

        //
        // How long Await will poll for the acquisition to finish before giving
        // up and reporting the capture as not completed. Unset means the
        // driver's own default (see DSOX1202G::kDefaultCaptureTimeout).
        //
        std::optional<core::quantities::Time>       Timeout{};

        //
        // How long Arm will poll :AER? for the scope to report itself armed
        // and ready. A separate number from the one above because it bounds a
        // different thing -- the instrument getting ready, which takes as long
        // as it takes regardless of the DUT, against the event arriving, which
        // is entirely about the DUT.
        //
        std::optional<core::quantities::Time>       ArmTimeout{};
    };

    class SingleBuilder : public ConfigBuilder<SingleBuilder, SingleConfig>
    {
        public:
            using Config = SingleConfig;

            explicit SingleBuilder( DSOX1202G & instrument) : ConfigBuilder( Config{ instrument }) {}

            [[nodiscard]]
            auto timeout( const core::quantities::Time value) const
            {
                return with( &Config::Timeout, value);
            }

            [[nodiscard]]
            auto armTimeout( const core::quantities::Time value) const
            {
                return with( &Config::ArmTimeout, value);
            }

    };
} // namespace hal::keysight_dsox1202g
