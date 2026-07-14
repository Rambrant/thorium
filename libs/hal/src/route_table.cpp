#include "hal/route_table.hpp"

#include <stdexcept>

namespace hal
{
    auto RouteTable::addRoute( const VpcLocation location, const InstrumentId instrument, const core::QuantityKind kind, std::vector<SwitchElementId> path) -> void
    {
        mEntries.push_back( RouteEntry{ location, instrument, kind, std::move( path)});
    }

    auto RouteTable::find( const VpcLocation location, const InstrumentId instrument, const core::QuantityKind kind) const -> const std::vector<SwitchElementId> &
    {
        for( const auto & entry : mEntries)
        {
            if( entry.location == location && entry.instrument == instrument && entry.kind == kind)
            {
                return entry.path;
            }
        }

        throw std::runtime_error(
            "hal::RouteTable: no route from " + to_string( location) + " to instrument " +
            std::string( to_string( instrument)) + "'s " + std::string( to_string( kind)) +
            " port -- that instrument isn't wired to that pin on this rig");
    }
} // namespace hal
