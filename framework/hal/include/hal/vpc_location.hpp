#pragma once

#include <cstdint>
#include <string>

namespace hal
{
    //
    // The VPC90 connector array is organized as two racks, A and B, each
    // holding a row of connectors numbered from the left (A1, A2, ..., B1,
    // B2, ...). A VpcLocation names one pin on one connector -- this is
    // where the DUT adapter physically plugs in, and is the coordinate every
    // hal::RouteTable entry (see route_table.hpp) is keyed against on one
    // side.
    //
    enum class VpcRack
    {
        A,
        B
    };

    struct VpcLocation
    {
        VpcRack       rack;
        std::uint16_t connector;
        std::uint16_t pin;

        friend constexpr auto operator==( VpcLocation, VpcLocation) -> bool = default;
    };

    [[nodiscard]]
    auto to_string( VpcLocation location) -> std::string;
} // namespace hal
