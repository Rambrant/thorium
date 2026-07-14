#include "hal/instrument.hpp"

namespace hal
{
    auto to_string( const InstrumentId id) -> std::string_view
    {
        switch( id)
        {
            case InstrumentId::Osc1:         return "Osc1";
            case InstrumentId::Dmm1:         return "Dmm1";
            case InstrumentId::Dmm2:         return "Dmm2";
            case InstrumentId::PowerSupply1: return "PowerSupply1";
        }

        return "UnknownInstrument";
    }
} // namespace hal
