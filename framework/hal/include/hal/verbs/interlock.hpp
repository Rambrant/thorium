#pragma once

#include <string>

#include "hal/topology/vpc_location.hpp"

namespace hal
{
    //
    // The rig-side half of the electrical interlock: which energised source is
    // cabled onto this VPC pin, if any.
    //
    // core::MeasureEngine asks this before it routes a reading that requires a
    // dead node -- an ammeter or an ohmmeter -- to a pin declared SOURCE_POINT,
    // and refuses the reading if the answer is not empty. See
    // core/verbs/interlock.hpp, which is where the argument for refusing that one
    // hazard (and merely recording hot switching) is written out; this file is
    // only the lookup that makes it answerable.
    //
    // Empty means "nothing live here", and covers both ordinary answers: no
    // source lands on this pin at all, or the one that does is sitting with its
    // output off. Non-empty is that source's instrument id, spelled the way
    // hal::to_string( InstrumentId) spells it, so the refusal message can name
    // the rail a script has to Remove before it measures.
    //
    // A std::string rather than an InstrumentId, and that is what keeps
    // core/verbs/measure.hpp free of any hal:: dependency: the caller only ever puts
    // this in a message, and a hal enumerator crossing that boundary would
    // need core to know the enum. Same reasoning as to_string( instrumentId),
    // which that file already reaches for by ADL and for the same reason.
    //
    // ---------------------------------------------------------------------
    // Why this is a plain declaration and not a template
    // ---------------------------------------------------------------------
    //
    // Because answering it takes two things a script's translation unit does
    // not have and must not gain. hal::sourceWiring exists only where the
    // rig's wiring.inc has been expanded, and the instruments' own live state
    // only where that rig's active_instruments.hpp has -- both of which stop
    // at hal/src/verbs/interlock.cpp on purpose (see hal/src/verbs/measure.cpp and
    // hal/src/verbs/route.cpp, which are the same shape for the same reason, and
    // framework/hal/CMakeLists.txt on why that is a target boundary rather than a
    // convention). A template would have to be instantiated where the caller
    // is, which is exactly where neither table is visible.
    //
    // So the call from core is an ordinary runtime one, resolved by ADL on
    // VpcLocation and satisfied by the linker. Declared here rather than in
    // hal/topology/wiring.hpp, next to the SourceWiring table it consults, because it
    // is not a wiring fact: wiring is fixed by whoever cabled the bench, and
    // this changes every time a script calls Apply or Remove.
    //
    [[nodiscard]]
    auto energisedSourceAt( VpcLocation location) -> std::string;
} // namespace hal
