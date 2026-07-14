#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/quantity_kind.hpp"
#include "hal/vpc_location.hpp"

namespace dut
{
    //
    // One named connection on this DUT/adapter: the pin the device is
    // connected to (name, e.g. "5VInput"), where that pin lands on the VPC90
    // array, and what kind of quantity is expected there. Because there is
    // exactly one adapter per device (the connector on the device doesn't
    // change), this table -- not some separate Adapter type -- *is* the
    // adapter.
    //
    struct AdapterPoint
    {
        std::string_view    name;
        hal::VpcLocation    location;
        core::QuantityKind  kind;
        std::string_view    description;
    };

    //
    // A fixed collection of named points for one DUT/adapter. Runtime-
    // constructed and runtime-searched for now (no MATRIX/POINT-style macros
    // yet, and no reflection) -- see the TODO in dut/measure.hpp for what
    // changes here once that lands.
    //
    class Adapter
    {
        public:
            Adapter( std::string_view name, std::string_view description, std::vector<AdapterPoint> points) :
                mName( name),
                mDescription( description),
                mPoints( std::move( points))
            {}

            [[nodiscard]]
            auto name() const -> std::string_view
            {
                return mName;
            }

            [[nodiscard]]
            auto description() const -> std::string_view
            {
                return mDescription;
            }

            [[nodiscard]]
            auto find( std::string_view pointName) const -> std::optional<AdapterPoint>
            {
                for( const auto & point : mPoints)
                {
                    if( point.name == pointName)
                    {
                        return point;
                    }
                }

                return std::nullopt;
            }

        private:
            std::string_view          mName;
            std::string_view          mDescription;
            std::vector<AdapterPoint> mPoints;
    };
} // namespace dut
