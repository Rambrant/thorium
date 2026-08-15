#include "hal/wiring.hpp"

#include <stdexcept>

namespace hal
{
    auto InstrumentWiring::addWire( const InstrumentId instrument, const SwitchElementId channel, const WireRole role) -> void
    {
        addWire( instrument, Path{ channel }, role);
    }

    auto InstrumentWiring::addWire( const InstrumentId instrument, Path path, const WireRole role) -> void
    {
        mEntries.push_back( InstrumentWiringEntry{ instrument, role, std::move( path) });
    }

    auto InstrumentWiring::find( const InstrumentId instrument) const -> Path
    {
        for( const auto & entry : mEntries)
        {
            if( entry.instrument == instrument && entry.role == WireRole::Force)
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

    auto InstrumentWiring::findSense( const InstrumentId instrument) const -> Path
    {
        Path combined;

        for( const auto & entry : mEntries)
        {
            if( entry.instrument == instrument && entry.role == WireRole::Sense)
            {
                combined.insert( combined.end(), entry.path.begin(), entry.path.end());
            }
        }

        if( combined.empty())
        {
            throw std::runtime_error(
                "hal::InstrumentWiring: instrument " + std::string( to_string( instrument)) +
                " has no fixed sense path on this rig's fabric");
        }

        return combined;
    }

    auto ConnectorWiring::addWire( const VpcLocation location, const SwitchElementId channel, const WireRole role) -> void
    {
        addWire( location, Path{ channel }, role);
    }

    auto ConnectorWiring::addWire( const VpcLocation location, Path path, const WireRole role) -> void
    {
        mEntries.push_back( ConnectorWiringEntry{ location, role, std::move( path) });
    }

    auto ConnectorWiring::find( const VpcLocation location) const -> Path
    {
        for( const auto & entry : mEntries)
        {
            if( entry.location == location && entry.role == WireRole::Force)
            {
                return entry.path;
            }
        }

        throw std::runtime_error(
            "hal::ConnectorWiring: " + to_string( location) + " has no fixed path on this rig's fabric");
    }

    auto ConnectorWiring::findSense( const VpcLocation location) const -> Path
    {
        for( const auto & entry : mEntries)
        {
            if( entry.location == location && entry.role == WireRole::Sense)
            {
                return entry.path;
            }
        }

        throw std::runtime_error(
            "hal::ConnectorWiring: " + to_string( location) + " has no fixed sense path on this rig's fabric");
    }

    auto SourceWiring::addLanding( const InstrumentId instrument, const VpcLocation location) -> void
    {
        mEntries.push_back( SourceWiringEntry{ instrument, location });
    }

    auto SourceWiring::find( const VpcLocation location) const -> InstrumentId
    {
        for( const auto & entry : mEntries)
        {
            if( entry.location == location)
            {
                return entry.instrument;
            }
        }

        throw std::runtime_error(
            "hal::SourceWiring: no fixed-wired source lands on " + to_string( location));
    }

    auto SourceWiring::findAll( const InstrumentId instrument) const -> std::vector<VpcLocation>
    {
        std::vector<VpcLocation> locations;

        for( const auto & entry : mEntries)
        {
            if( entry.instrument == instrument)
            {
                locations.push_back( entry.location);
            }
        }

        //
        // No throw on empty -- see this method's own comment in
        // hal/wiring.hpp. Every routed instrument on the rig lands nowhere,
        // and that is the correct answer rather than an error.
        //
        return locations;
    }
} // namespace hal
