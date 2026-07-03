#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "core/switch_matrix.hpp"

namespace dut
{
    //
    // Which rig instrument a test point routes to. The DUT names the instrument
    // abstractly by capability; the concrete driver is resolved by the rig.
    //
    enum class Instrument
    {
        Oscilloscope,
        Voltmeter,
        PowerSupply
    };

    //
    // A single logical test point: a human-readable name (mirroring the test
    // spec, e.g. "5Vdc_port"), the abstract instrument it needs, and the matrix
    // crosspoint that routes that instrument to this point. The crosspoint
    // coordinates come from the rig's fixed fabric (hal); this table records
    // which one each logical point uses.
    //
    struct TestPoint
    {
        std::string_view name;
        Instrument       instrument;
        core::Crosspoint crosspoint;
        std::string_view description;
    };

    //
    // A fixed collection of test points for one DUT/adapter combination.
    // Because there is exactly one adapter type per device, this map is a
    // compile-time constant declared via the MATRIX/POINT macros below.
    //
    template< std::size_t N>
    struct MatrixMap
    {
        std::string_view         name;
        std::string_view         description;
        std::array<TestPoint, N> points;

        [[nodiscard]]
        constexpr auto find( std::string_view point_name ) const -> std::optional< TestPoint>
        {
            for( const auto & point : points)
            {
                if( point.name == point_name)
                {
                    return point;
                }
            }

            return std::nullopt;
        }
    };
} // namespace dut

//
// MATRIX / POINT / END_MATRIX: declarative routing table, deliberately
// mirroring the GROUP / CRIT style used for criteria so the two feel of a
// piece. A DUT's routing lives in a header as a compile-time constant:
//
//   MATRIX(StdAdapter, "Standard adapter for Device X")
//       POINT(Port5Vdc, dut::Instrument::Voltmeter, 3, 7, "5Vdc supply port")
//       POINT(ClkProbe, dut::Instrument::Oscilloscope, 1, 2, "Config clock")
//   END_MATRIX
//
// expands to `constexpr auto StdAdapter = dut::MatrixMap<2>{...};`
//
// Implementation note: POINT entries are collected via a variadic list, so
// the macros are used as MATRIX(...) POINT(...) POINT(...) END_MATRIX with
// the POINTs comma-separated by the expansion.
//
#define MATRIX( mapName, desc)                                 \
    inline constexpr auto mapName = []                         \
    {                                                           \
        using ::dut::TestPoint;                                 \
        using ::dut::Instrument;                                \
        constexpr std::string_view MapName = #mapName;         \
        constexpr std::string_view MapDesc = desc;              \
        constexpr TestPoint kPoints[] = {

#define POINT( id, instr, row, col, desc) \
    TestPoint{ #id, instr, ::core::Crosspoint{row, col}, desc},

#define END_MATRIX                                                            \
        };                                                                    \
        constexpr std::size_t kN = sizeof(kPoints) / sizeof(kPoints[0]);      \
        ::dut::MatrixMap<kN> m{MapName, MapDesc, {}};                         \
        for (std::size_t i = 0; i < kN; ++i) m.points[i] = kPoints[i];        \
        return m;                                                             \
    }();
