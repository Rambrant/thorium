#pragma once

#include <string_view>

namespace hal
{
    //
    // The fixed, static list of instruments a rig actually has. Several
    // instruments can share the same underlying type (e.g. a rig's Dmm1 and
    // Dmm2 might both be hal::L4411A -- see hal/l4411a.hpp) and be
    // interchanged during a test -- InstrumentId is what a hal::RouteTable
    // entry and a recorded sample identify the instrument by, since two
    // Dmms are two distinct wiring facts even though they're the same C++
    // type.
    //
    // This header is generic -- shared by every rig that links hal -- so
    // the actual enumerators (Dmm1, DcP1, AcP1, ...) are not listed here at
    // all. They're generated from THORIUM_INSTRUMENT_TABLE, a compile
    // definition pointing at the linking rig's own instrument.inc (see
    // rig/instrument.inc for this repo's) -- the same compile-definition-
    // swap mechanism core/active_criteria.hpp already uses for
    // THORIUM_ACTIVE_CRITERIA. A driver still names the *role* it plays
    // ("DC power, channel N"), not the model, the same reasoning that keeps
    // hal::N6701A's own class name (not this enum) tied to the physical
    // instrument model -- see hal::N6701A's own comment in hal/n6701a.hpp.
    //
    // instrument.inc is read here with INSTRUMENT redefined to keep only
    // the id token -- the same file rig/active_instruments.hpp declares the
    // real instrument globals from (INSTRUMENT(type, id, ...), one token
    // for both the global's name and its identity -- see that file's own
    // comment on why there's no separate name parameter to ignore here) and
    // hal/src/safing.cpp calls safe() on each from. There used to be a
    // separate rig/instrument_id.inc for just this list; folding it into
    // this one expansion instead means an instrument added to instrument.inc
    // can never desync from its own InstrumentId enumerator -- there's
    // nothing left to desync from.
    //
    // push_macro/pop_macro rather than a bare #define/#undef: instrument.inc
    // starts with INSTRUMENTS and ends with END_INSTRUMENTS (see
    // rig/active_instruments.hpp), so both need to be defined here too,
    // regardless of whether this header is the first to touch them in a
    // given translation unit or active_instruments.hpp already has --
    // exactly the guard hal/src/safing.cpp already needs around its own
    // second read of instrument.inc, for the same reason.
    //
#pragma push_macro( "INSTRUMENTS")
#pragma push_macro( "INSTRUMENT")
#pragma push_macro( "END_INSTRUMENTS")
#undef INSTRUMENTS
#undef INSTRUMENT
#undef END_INSTRUMENTS

#define INSTRUMENTS
#define INSTRUMENT( type, id, ...) id,
#define END_INSTRUMENTS

    enum class InstrumentId
    {
        #include THORIUM_INSTRUMENT_TABLE
    };

#pragma pop_macro( "END_INSTRUMENTS")
#pragma pop_macro( "INSTRUMENT")
#pragma pop_macro( "INSTRUMENTS")

    [[nodiscard]]
    auto to_string( InstrumentId id) -> std::string_view;

    //
    // Every instrument in this rig can be dropped to a known idle state
    // without being told anything about what was running -- no config, no
    // builder chain, no point, no return value. hal::safeRig() (see
    // hal/safing.hpp) is the only caller.
    //
    // Deliberately a plain member rather than an ADL customization point
    // in the shape of applyDriver/connectDriver: those are addressed
    // through a config type precisely because Apply/Connect need to know
    // *what* to apply, and there is a builder chain to carry it. Safing
    // has no such argument to carry by construction -- it is the operation
    // you invoke when nobody knows what state the rig reached.
    //
    // This concept exists for two things, neither of which is dispatch.
    // First, so hal::safeRig()'s per-instrument expansion can
    // static_assert it and produce a one-line message naming the offending
    // instrument, rather than a page of overload-resolution output.
    // Second, so the guarantee can be tested in both directions -- see
    // hal/tests/test_safing.cpp, which asserts that a driver *without*
    // safe() fails the concept, the only way to demonstrate that the
    // compile-time half of the safing contract is real without breaking
    // the build. Note it is not used to *skip* instruments that don't
    // satisfy it: see hal::L4411A::safe() for why opt-in safing is the
    // wrong shape here.
    //
    template<typename InstrumentT>
    concept SafeableInstrument = requires( InstrumentT & instrument)
    {
        instrument.safe();
    };
} // namespace hal
