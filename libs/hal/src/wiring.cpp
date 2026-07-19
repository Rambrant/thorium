#include "hal/wiring.hpp"

#include <stdexcept>

namespace hal
{
    auto InstrumentWiring::addWire( const InstrumentId instrument, const SwitchElementId channel) -> void
    {
        mEntries.push_back( InstrumentWiringEntry{ instrument, channel });
    }

    auto InstrumentWiring::find( const InstrumentId instrument) const -> SwitchElementId
    {
        for( const auto & entry : mEntries)
        {
            if( entry.instrument == instrument)
            {
                return entry.channel;
            }
        }

        throw std::runtime_error(
            "hal::InstrumentWiring: instrument " + std::string( to_string( instrument)) +
            " has no fixed channel on this rig's fabric");
    }

    auto InstrumentWiring::findAll( const InstrumentId instrument) const -> std::vector<SwitchElementId>
    {
        std::vector<SwitchElementId> channels;

        for( const auto & entry : mEntries)
        {
            if( entry.instrument == instrument)
            {
                channels.push_back( entry.channel);
            }
        }

        if( channels.empty())
        {
            throw std::runtime_error(
                "hal::InstrumentWiring: instrument " + std::string( to_string( instrument)) +
                " has no fixed channel on this rig's fabric");
        }

        return channels;
    }

    auto ConnectorWiring::addWire( const VpcLocation location, const SwitchElementId channel) -> void
    {
        mEntries.push_back( ConnectorWiringEntry{ location, channel });
    }

    auto ConnectorWiring::find( const VpcLocation location) const -> SwitchElementId
    {
        for( const auto & entry : mEntries)
        {
            if( entry.location == location)
            {
                return entry.channel;
            }
        }

        throw std::runtime_error(
            "hal::ConnectorWiring: " + to_string( location) + " has no fixed channel on this rig's fabric");
    }
} // namespace hal
