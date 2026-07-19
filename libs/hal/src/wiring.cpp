#include "hal/wiring.hpp"

#include <stdexcept>

namespace hal
{
    auto InstrumentWiring::addWire( const InstrumentId instrument, const SwitchElementId channel) -> void
    {
        addWire( instrument, Path{ channel });
    }

    auto InstrumentWiring::addWire( const InstrumentId instrument, Path path) -> void
    {
        mEntries.push_back( InstrumentWiringEntry{ instrument, std::move( path) });
    }

    auto InstrumentWiring::find( const InstrumentId instrument) const -> Path
    {
        for( const auto & entry : mEntries)
        {
            if( entry.instrument == instrument)
            {
                return entry.path;
            }
        }

        throw std::runtime_error(
            "hal::InstrumentWiring: instrument " + std::string( to_string( instrument)) +
            " has no fixed path on this rig's fabric");
    }

    auto InstrumentWiring::findAll( const InstrumentId instrument) const -> Path
    {
        Path combined;

        for( const auto & entry : mEntries)
        {
            if( entry.instrument == instrument)
            {
                combined.insert( combined.end(), entry.path.begin(), entry.path.end());
            }
        }

        if( combined.empty())
        {
            throw std::runtime_error(
                "hal::InstrumentWiring: instrument " + std::string( to_string( instrument)) +
                " has no fixed path on this rig's fabric");
        }

        return combined;
    }

    auto ConnectorWiring::addWire( const VpcLocation location, const SwitchElementId channel) -> void
    {
        addWire( location, Path{ channel });
    }

    auto ConnectorWiring::addWire( const VpcLocation location, Path path) -> void
    {
        mEntries.push_back( ConnectorWiringEntry{ location, std::move( path) });
    }

    auto ConnectorWiring::find( const VpcLocation location) const -> Path
    {
        for( const auto & entry : mEntries)
        {
            if( entry.location == location)
            {
                return entry.path;
            }
        }

        throw std::runtime_error(
            "hal::ConnectorWiring: " + to_string( location) + " has no fixed path on this rig's fabric");
    }
} // namespace hal
