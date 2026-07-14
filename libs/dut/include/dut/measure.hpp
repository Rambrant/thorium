#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "core/quantity_kind.hpp"
#include "core/session.hpp"
#include "dut/bench.hpp"
#include "hal/instrument.hpp"

namespace dut
{
    //
    // Ties a DUT-side name (e.g. "5VOutput") to a hal instrument port (e.g.
    // dmm1.voltage()) and fetches the value through whatever session the
    // Bench holds:
    //
    //   Measure( bench, dmm1.voltage(), "5VOutput");
    //
    // Three things are checked, in order, all as runtime errors today:
    //   1. the name exists on this DUT's Adapter
    //   2. the Adapter's declared quantity for that name matches QuantityT
    //   3. the rig's RouteTable actually wires this instrument to that pin
    //
    // TODO(reflection): (1) and (2) are runtime lookups/checks only because
    // the Adapter table is a plain runtime vector, searched by a
    // std::string_view that's an ordinary function parameter -- there is no
    // macro/reflection layer yet giving the compiler a compile-time picture of
    // "this name resolves to this location with this quantity". Once the
    // reflection-based get<"id">() work in core/criterion.hpp is verified on
    // GCC 16, both checks become candidates to move to compile-time
    // static_asserts (alongside the RouteTable lookup in hal/route_table.hpp,
    // which has the matching TODO). Until then, a script that misspells a
    // point name or asks for the wrong quantity fails when Measure is called,
    // not at compile time.
    //
    template<core::quantities::QuantityType QuantityT, typename InstrumentT>
    [[nodiscard]]
    auto Measure( Bench & bench, hal::Port<QuantityT, InstrumentT> port, std::string_view pointName) -> QuantityT
    {
        const auto point = bench.adapter().find( pointName);

        if( !point)
        {
            throw std::runtime_error(
                "dut::Measure: adapter '" + std::string( bench.adapter().name()) +
                "' has no point named '" + std::string( pointName) + "'");
        }

        const auto expectedKind = core::quantityKindOf<QuantityT>();

        if( point->kind != expectedKind)
        {
            throw std::runtime_error(
                "dut::Measure: point '" + std::string( pointName) + "' is declared as " +
                std::string( core::to_string( point->kind)) + " but was measured as " +
                std::string( core::to_string( expectedKind)));
        }

        const auto instrumentId = port.instrumentId();

        auto liveRead = [&]() -> core::QuantityVariant
        {
            const auto & path = bench.routes().find( point->location, instrumentId, expectedKind);
            bench.fabric().route( path);
            return core::QuantityVariant{ port.rawMeasure() };
        };

        auto value = bench.session().fetch( pointName, hal::to_string( instrumentId), expectedKind, liveRead);

        return core::asQuantity<QuantityT>( value);
    }
} // namespace dut
