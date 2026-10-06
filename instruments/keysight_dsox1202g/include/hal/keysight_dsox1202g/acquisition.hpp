#pragma once

#include <optional>

#include "hal/driver/builder.hpp"

#include "hal/keysight_dsox1202g/vocabulary.hpp"

//
// :ACQuire -- AcquisitionConfig and AcquisitionBuilder.
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
    // :ACQuire -- how the samples that make up the record are taken.
    //
    // Two fields, where the Infiniium's equivalent has seven, and every one of
    // the five missing ones is missing because this instrument does not have
    // the setting:
    //
    //   memory depth   :ACQuire:POINts? is query-only here. How deep the
    //                  record is follows from the timebase and the mode; there
    //                  is no :ACQuire:POINts command and no :POINts:AUTO to
    //                  turn off, so points() and automaticPoints() have
    //                  nothing to send. (:WAVeform:POINts, which does exist,
    //                  is a different fact -- how much of the record to
    //                  transfer -- and belongs to the fetch, see
    //                  WaveformConfig.)
    //   sample rate    :ACQuire:SRATe? is query-only for the same reason.
    //   averaging      one of the types now, not a flag beside them -- see
    //                  AcquisitionType.
    //
    // A script ported from the other scope loses those calls at compile time
    // rather than having them silently accepted and ignored, which is the
    // whole reason this is a separate driver.
    //
    struct AcquisitionConfig
    {
        DSOX1202G &                                 Instrument;
        std::optional<AcquisitionType>              Type{};

        //
        // :ACQuire:COUNt, 2 to 65536 -- how many acquisitions are averaged
        // together, and meaningful only when Type is Averaged. Set through
        // averagedOver() on the builder, which sets both, so that "average,
        // over however many you were last told" is not expressible: that count
        // is inherited instrument state, and a reproducible test must not
        // depend on it.
        //
        std::optional<unsigned>                     AverageCount{};
    };

    class AcquisitionBuilder : public ConfigBuilder<AcquisitionBuilder, AcquisitionConfig>
    {
        public:
            using Config = AcquisitionConfig;

            explicit AcquisitionBuilder( DSOX1202G & instrument) : ConfigBuilder( Config{ instrument }) {}

            //
            // type(), not mode(): :ACQuire:TYPE is the command, and this
            // instrument's :ACQuire:MODE is a different setting -- see
            // AcquisitionType. A script ported from the Infiniium driver hits
            // this rename at compile time, which is the right place to be
            // asked whether it meant the sampling type or real-time-against-
            // segmented.
            //
            [[nodiscard]]
            auto type( const AcquisitionType value) const
            {
                return with( &Config::Type, value);
            }

            //
            // Averaging on, over this many acquisitions -- one call, because
            // on this instrument it is one decision in two commands
            // (:ACQuire:TYPE AVERage and :ACQuire:COUNt). Setting the count
            // without the type would leave the count sitting unused until some
            // later script selected averaging and inherited it.
            //
            // Note this averages across successive triggers, so a single-shot
            // capture cannot use it -- there is only ever one trigger to
            // average. AcquisitionType::HighResolution is the one that
            // averages *within* a record, and is what a single-shot capture of
            // a slow event wants instead.
            //
            [[nodiscard]]
            auto averagedOver( const unsigned count) const
            {
                return changed( [count]( Config & config)
                                {
                                    config.Type         = AcquisitionType::Averaged;
                                    config.AverageCount = count;
                                });
            }

    };
} // namespace hal::keysight_dsox1202g
