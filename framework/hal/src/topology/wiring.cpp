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

    auto SourceWiring::addLanding( const InstrumentId instrument, const VpcLocation location) -> void
    {
        mEntries.push_back( SourceWiringEntry{ instrument, location });
    }

    auto SourceWiring::findLanding( const VpcLocation location) const -> std::optional<InstrumentId>
    {
        for( const auto & entry : mEntries)
        {
            if( entry.location == location)
            {
                return entry.instrument;
            }
        }

        return std::nullopt;
    }

    auto SourceWiring::find( const VpcLocation location) const -> InstrumentId
    {
        // Expressed through findLanding rather than walking mEntries a second
        // time, so the two answers cannot come to differ about what "lands
        // here" means -- the throw is the only thing this adds.
        if( const auto found = findLanding( location))
        {
            return *found;
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
        // hal/topology/wiring.hpp. Every routed instrument on the rig lands nowhere,
        // and that is the correct answer rather than an error.
        //
        return locations;
    }

    auto TapWiring::addTap( const InstrumentId instrument, const VpcLocation location) -> void
    {
        mEntries.push_back( TapWiringEntry{ instrument, location });
    }

    auto TapWiring::taps( const InstrumentId instrument) const -> bool
    {
        for( const auto & entry : mEntries)
        {
            if( entry.instrument == instrument)
            {
                return true;
            }
        }

        return false;
    }

    auto TapWiring::isTappedBy( const InstrumentId instrument, const VpcLocation location) const -> bool
    {
        for( const auto & entry : mEntries)
        {
            if( entry.instrument == instrument && entry.location == location)
            {
                return true;
            }
        }

        return false;
    }

    auto TapWiring::findAll( const InstrumentId instrument) const -> std::vector<VpcLocation>
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
        // No throw on empty, exactly as SourceWiring::findAll() has none, and
        // for the same reason: "this instrument taps nothing" is the correct
        // answer for every routed instrument on a rack rig.
        //
        return locations;
    }

    auto TapWiring::describeTaps( const InstrumentId instrument) const -> std::string
    {
        //
        // "none" rather than an empty string, because the one caller is a
        // refusal message (see core::MeasureEngine) and an empty tail there
        // would read as a truncated sentence rather than as an answer. That
        // caller only asks about instruments taps() said yes to, so "none" is
        // not reachable through it -- it is what this returns for anyone else
        // who asks, rather than a case the message has to guard.
        //
        const auto locations = findAll( instrument);

        if( locations.empty())
        {
            return "none";
        }

        std::string described;

        for( const auto & location : locations)
        {
            if( ! described.empty())
            {
                described += ", ";
            }

            described += to_string( location);
        }

        return described;
    }
} // namespace hal
