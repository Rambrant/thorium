#include "hal/topology/wiring.hpp"

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

    //
    // The one thing this table does that the shared one below it does not: a
    // caller that has already established a pin is a landing pin wants the
    // throw, since reaching it means an invariant broke somewhere upstream.
    //
    // Expressed through findLanding rather than walking the rows a second
    // time, so the two answers cannot come to differ about what "lands here"
    // means -- the throw is the only thing this adds.
    //
    auto SourceWiring::find( const VpcLocation location) const -> InstrumentId
    {
        if( const auto found = findLanding( location))
        {
            return *found;
        }

        throw std::runtime_error(
            "hal::SourceWiring: no fixed-wired source lands on " + to_string( location));
    }

} // namespace hal
