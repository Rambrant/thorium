#pragma once

#include <string>

#include "core/journal.hpp"

//
// The describeConfig customization point: what a driver's config type says
// about itself when a verb needs to be logged, and the one helper that turns
// that answer into a journal event.
//
// Neither belongs to any single verb, which is why they are here rather than in
// one of the files that uses them. Every verb that acts on an instrument config
// needs this: the sourcing verbs (core/source.hpp), the routing verbs
// (core/route.hpp) and the byte-oriented ones (core/transfer.hpp). Putting the
// contract in whichever of those happened to be written first would have the
// other two including it for a reason their include list does not admit to.
//
// The overloads themselves are hal-side, one per concrete config type -- see
// hal/n6701a.hpp, hal/ac6834b.hpp and hal/racal1260.hpp for the bodies, and
// hal/describe.hpp for the formatting helpers they share. This header names the
// contract and nothing else, which is what keeps core free of any hal::
// dependency.
//
namespace core
{
    //
    // What an ADL describeConfig(config) hands back for the log -- the two
    // things a log needs about a sourcing or routing step that the config type
    // itself is the only thing able to answer: which instrument, and what it
    // was told to do.
    //
    // A struct rather than one string, because the two are used differently:
    // the instrument id is a key a consumer groups and filters by (it matches
    // hal::InstrumentId's own spelling, and a Measure's Instrument field), while
    // the settings text is prose for a reader. Flattening them would leave a
    // machine consumer parsing "DcP1: 24 V, limit 7 A" apart again.
    //
    // Empty Settings is normal and correct: Remove/Connect/Disconnect have
    // nothing to report beyond which instrument they acted on (removeDriver
    // only ever reads Instrument -- see hal/n6701a.hpp), so only Apply and Setup
    // ask for this half at all.
    //
    struct SourceDescription
    {
        std::string Instrument;
        std::string Settings;
    };

    namespace detail
    {
        //
        // Posts one sourcing/routing event. describeConfig is an unqualified
        // call, so it resolves via ADL to the hal-side overload for the concrete
        // config type -- exactly the customization point applyDriver/
        // connectDriver already are (see core/source.hpp and core/route.hpp),
        // which is what keeps this header free of any hal:: dependency while
        // still logging instrument-specific detail.
        //
        // withSettings is a compile-time choice made by the calling engine
        // rather than something describeConfig decides, so a driver author has
        // one function to write per config type instead of one per verb.
        //
        template<typename ConfigT>
        auto postSourceEvent( const Verb method, const ConfigT & config, const bool withSettings) -> void
        {
            const auto described = describeConfig( config);

            journal().post( JournalRecord{
                .Method     = method,
                .Subject    = described.Instrument,
                .Instrument = described.Instrument,
                .Value      = withSettings ? described.Settings : std::string{}
            });
        }
    } // namespace detail
} // namespace core
