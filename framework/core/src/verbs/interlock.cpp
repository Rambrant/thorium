#include "core/verbs/interlock.hpp"

namespace core
{
    auto hotSwitchDetail( const Verb method, const std::string_view instrument) -> std::string
    {
        //
        // "hot switching" leads, so the thing a reader is scanning for is the
        // first thing on the line and grep finds every occurrence with one
        // pattern regardless of which half happened. What follows says which
        // half, in the terms core/verbs/source.hpp describes them in.
        //
        const auto what = method == Verb::Connect
                              ? "relay closed onto an energised output"
                              : "relay opened while the output was energised";

        return "hot switching -- " + std::string( what) + " (" + std::string( instrument) + ")";
    }

    auto liveTapMessage( const std::string_view point,
                         const std::string_view instrument,
                         const QuantityKind     kind,
                         const std::string_view liveSource) -> std::string
    {
        //
        // Names the remedy as well as the fault. The one thing this message
        // is for is a script author reading it out of a failed run, and the
        // fix is always the same shape -- de-energise the rail, then measure
        // -- so saying it here costs a clause and saves a trip through
        // core/verbs/interlock.hpp's comment.
        //
        return "interlock: refusing to route " + std::string( instrument) + "'s " +
               std::string( to_string( kind)) + " measurement to " + std::string( point) +
               " -- " + std::string( liveSource) + " is energised and cabled onto that pin, so this"
               " reading would put a low impedance across a live rail through the switching fabric."
               " Remove( " + std::string( liveSource) + ") first, or measure at the instrument instead";
    }
} // namespace core
