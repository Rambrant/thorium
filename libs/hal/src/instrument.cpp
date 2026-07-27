#include "hal/instrument.hpp"

namespace hal
{
    //
    // Same THORIUM_INSTRUMENT_IDS content hal/instrument.hpp already
    // expanded once, to declare InstrumentId's enumerators -- re-expanded
    // here with INSTRUMENT_ID redefined to build a switch case per name
    // instead, the same two-meanings-of-one-X-macro pattern
    // hal/src/safing.cpp uses on instrument.inc. Every enumerator gets a
    // case this way, so -Wswitch would catch a name added to
    // instrument_id.inc without a matching driver, or vice versa -- see
    // that file's own comment.
    //
    auto to_string( const InstrumentId id) -> std::string_view
    {
        switch( id)
        {
#define INSTRUMENT_ID( name) case InstrumentId::name: return #name;
#include THORIUM_INSTRUMENT_IDS
#undef INSTRUMENT_ID
        }

        return "UnknownInstrument";
    }
} // namespace hal
