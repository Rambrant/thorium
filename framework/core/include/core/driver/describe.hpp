#pragma once

#include <string>

#include "core/session/bench.hpp"
#include "core/verbs/interlock.hpp"
#include "core/journal/journal.hpp"

//
// The describeConfig customization point: what a driver's config type says
// about itself when a verb needs to be logged, and the one helper that turns
// that answer into a journal event.
//
// Neither belongs to any single verb, which is why they are here rather than in
// one of the files that uses them. Every verb that acts on an instrument config
// needs this: the sourcing verbs (core/verbs/source.hpp), the routing verbs
// (core/verbs/route.hpp) and the byte-oriented ones (core/verbs/transfer.hpp). Putting the
// contract in whichever of those happened to be written first would have the
// other two including it for a reason their include list does not admit to.
//
// The overloads themselves are hal-side, one per concrete config type -- see
// hal/n6701a.hpp, hal/ac6834b.hpp and hal/racal1260.hpp for the bodies, and
// hal/driver/describe.hpp for the formatting helpers they share. This header names the
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
        // connectDriver already are (see core/verbs/source.hpp and core/verbs/route.hpp),
        // which is what keeps this header free of any hal:: dependency while
        // still logging instrument-specific detail.
        //
        // withSettings is a compile-time choice made by the calling engine
        // rather than something describeConfig decides, so a driver author has
        // one function to write per config type instead of one per verb.
        //
        // hotSwitched defaults false because only the two routing verbs can be
        // it -- a relay is the only thing here that moves, so Apply, Remove,
        // Setup and Write have nothing to pass and are left untouched. It is
        // handed in rather than worked out here, unlike the bench flag below,
        // because answering it needs the ADL probe in core/verbs/interlock.hpp and
        // the engine has already had to ask (it decides nothing on the answer,
        // but the alternative is asking the driver the same question twice).
        //
        template<typename ConfigT>
        auto postSourceEvent( const Verb method, const ConfigT & config, const bool withSettings, const bool hotSwitched = false) -> void
        {
            const auto described = describeConfig( config);

            journal().post( JournalRecord{
                .Method     = method,
                .Subject    = described.Instrument,

                //
                // Says so when the instruction went nowhere -- one place, so
                // that a verb added later cannot post an unmarked event by
                // forgetting to. See kDetachedDetail in core/session/bench.hpp on why
                // the event is posted at all rather than suppressed.
                //
                // Read here rather than passed in by the engine, even though
                // the engine has just read the same flag to decide whether to
                // call its driver. Single-threaded by construction (one script
                // at a time, see framework/runner/src/main.cpp), so the two reads cannot
                // disagree -- and a parameter would be a second way to say it,
                // which an engine could set wrongly.
                //
                //
                // A detached run cannot hot switch, and the marker wins over
                // the note rather than joining it: nothing reached a relay, so
                // there was no contact to arc. The engines gate their own
                // detection on the same flag, so hotSwitched is already false
                // whenever this branch is taken -- writing the precedence out
                // here states it rather than leaving it resting on that.
                //
                .Detail     = bench().isAttached()
                                  ? ( hotSwitched ? hotSwitchDetail( method, described.Instrument)
                                                  : std::string{})
                                  : std::string( kDetachedDetail),
                .Instrument = described.Instrument,
                .Value      = withSettings ? described.Settings : std::string{}
            });
        }
    } // namespace detail
} // namespace core
