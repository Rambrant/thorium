#include "hal/instrument.hpp"

namespace hal
{
    auto to_string( const InstrumentId id) -> std::string_view
    {
        switch( id)
        {
            case InstrumentId::Osc1: return "Osc1";
            case InstrumentId::Dmm1: return "Dmm1";
            case InstrumentId::Dmm2: return "Dmm2";
            case InstrumentId::DcP1: return "DcP1";
            case InstrumentId::DcP2: return "DcP2";
            case InstrumentId::DcP3: return "DcP3";
            case InstrumentId::DcP4: return "DcP4";
            case InstrumentId::AcP1: return "AcP1";
        }

        return "UnknownInstrument";
    }
} // namespace hal
