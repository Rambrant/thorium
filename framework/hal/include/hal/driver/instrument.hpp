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
    // swap mechanism core/criteria/active_criteria.hpp already uses for
    // THORIUM_CRITERIA_VARIANT_TABLES. A driver still names the *role* it plays
    // ("DC power, channel N"), not the model, the same reasoning that keeps
    // hal::N6701A's own class name (not this enum) tied to the physical
    // instrument model -- see hal::N6701A's own comment in hal/n6701a.hpp.
    //
    // instrument.inc is read here with INSTRUMENT redefined to keep only
    // the id token -- the same file rig/active_instruments.hpp declares the
    // real instrument globals from (INSTRUMENT(type, id, address, ...), one
    // token for both the global's name and its identity -- see that file's
    // own comment on why there's no separate name parameter to ignore here).
    // The macro is spelled with all of that file's fixed columns even though
    // this read discards every one of them but the id: a row that forgot its
    // address column is then a "macro requires 3 arguments" error in both
    // readers rather than only in the one that builds the globals.
    // There used to be a separate rig/instrument_id.inc for just this list;
    // folding it into this one expansion instead means an instrument added
    // to instrument.inc can never desync from its own InstrumentId
    // enumerator -- there's nothing left to desync from.
    //
    // This redefine-the-macro-and-re-#include step is exactly the pattern
    // reflection replaced everywhere else in this codebase (see
    // core::meta::all()/get() in core/criteria/criterion.hpp, hal::isWired() in
    // hal/topology/wiring.hpp, the criteria-variant parity check in
    // suite/tests/test_criteria_variants_compile.cpp, and safeRig() below
    // via InstrumentTag) -- but it survives here on purpose, not because
    // nobody got to it. Every one of those replacements works by reflecting
    // over a C++ entity that already exists (a struct's members, an enum's
    // enumerators, a namespace's declarations) instead of re-parsing macro
    // invocations. Here, there is nothing yet to reflect over: generating
    // InstrumentId's own enumerators *is* the job of this expansion, so by
    // definition it has to run before InstrumentId exists, not after. This
    // is the one macro-redefinition-and-re-#include left in this codebase
    // that reflection cannot remove, a bootstrapping step rather than a
    // remaining instance of a solved problem. hal/src/verbs/safing.cpp used to
    // read instrument.inc a third time (INSTRUMENT redefined again, to call
    // safe() on each) for no such reason -- InstrumentId already existed by
    // then -- which is exactly why that one *did* get replaced; see
    // InstrumentTag's own comment below.
    //
    // push_macro/pop_macro rather than a bare #define/#undef: instrument.inc
    // starts with INSTRUMENTS and ends with END_INSTRUMENTS (see
    // rig/active_instruments.hpp), so both need to be defined here too,
    // regardless of whether this header is the first to touch them in a
    // given translation unit or active_instruments.hpp already has.
    //
#pragma push_macro( "INSTRUMENTS")
#pragma push_macro( "INSTRUMENT")
#pragma push_macro( "END_INSTRUMENTS")
#undef INSTRUMENTS
#undef INSTRUMENT
#undef END_INSTRUMENTS

#define INSTRUMENTS
#define INSTRUMENT( type, id, address, ...) id,
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
    // Empty marker base -- every concrete instrument driver (hal::L4411A,
    // hal::DSO8064A, hal::N6701A<Isolation>, hal::Ac6834B) inherits it
    // publicly, and nothing else does. hal::safeRig() (hal/src/verbs/safing.cpp)
    // is the only reason this exists: it reflects over the global namespace
    // for every InstrumentTag-derived variable (rig/active_instruments.hpp
    // declares those globals unqualified, not inside a struct/namespace
    // this-rig-only code could instead reflect over directly) and calls
    // safe() on each, rather than re-reading rig/instrument.inc a third time
    // with INSTRUMENT redefined. An empty, non-virtual base costs nothing
    // (empty base optimization), needs no constructor changes anywhere (a
    // driver's existing mem-initializer list already default-constructs it
    // implicitly), and -- unlike matching on "has a safe() member", which
    // would sweep in any unrelated global that happened to also spell a
    // method safe() -- only ever matches a type that explicitly opted in by
    // inheriting this.
    //
    struct InstrumentTag {};

    //
    // Every instrument in this rig can be dropped to a known idle state
    // without being told anything about what was running -- no config, no
    // builder chain, no point, no return value. hal::safeRig() (see
    // hal/verbs/safing.hpp) is the only caller.
    //
    // Deliberately a plain member rather than an ADL customization point
    // in the shape of applyDriver/connectDriver: those are addressed
    // through a config type precisely because Apply/Connect need to know
    // *what* to apply, and there is a builder chain to carry it. Safing
    // has no such argument to carry by construction -- it is the operation
    // you invoke when nobody knows what state the rig reached.
    //
    // This concept exists for two things, neither of which is dispatch.
    // First, so hal::safeRig()'s reflection-driven loop over
    // InstrumentTag-derived globals can static_assert it per instrument and
    // produce a one-line message naming the offending type, rather than a
    // page of overload-resolution output. Second, so the guarantee can be
    // tested in both directions -- see rig/tests/test_safing.cpp, which
    // asserts that a driver *without* safe() fails the concept, the only
    // way to demonstrate that the compile-time half of the safing contract
    // is real without breaking the build. Note it is not used to *skip*
    // instruments that don't satisfy it: see hal::L4411A::safe() for why
    // opt-in safing is the wrong shape here.
    //
    template<typename InstrumentT>
    concept SafeableInstrument = requires( InstrumentT & instrument)
    {
        instrument.safe();
    };
} // namespace hal
