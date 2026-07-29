#include "hal/instrument.hpp"

#include "core/meta.hpp"

namespace hal
{
    //
    // Used to re-expand instrument.inc a second time (with INSTRUMENT
    // redefined to build a switch case per id, the same two-meanings-of-
    // one-X-macro pattern hal/src/safing.cpp still uses on it). core::meta::
    // to_string() (core/meta.hpp) replaces that: it reflects over
    // InstrumentId's own enumerators, so there is no separate name list here
    // to keep in sync with instrument.inc at all, rather than one -Wswitch
    // already caught if it drifted.
    //
    auto to_string( const InstrumentId id) -> std::string_view
    {
        return core::meta::to_string( id);
    }
} // namespace hal
