#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/quantity_kind.hpp"

namespace core
{
    //
    // One named connection on a DUT/adapter: the pin the device is connected
    // to (name, e.g. "5VInput"), where that pin lands physically, and what
    // kind of quantity is expected there. Generic over LocationT -- this
    // header has no idea what a physical location coordinate looks like,
    // only that it's some comparable value; hal::VpcLocation (see
    // hal/vpc_location.hpp) is simply the concrete coordinate type this
    // project's rig happens to use, not something this type depends on.
    //
    template<typename LocationT>
    struct AdapterPoint
    {
        std::string_view    name;
        LocationT           location;
        QuantityKind        kind;
        std::string_view    description;
    };

    //
    // A fixed collection of named points for one DUT/adapter. Because there
    // is exactly one adapter per device (the connector on the device doesn't
    // change), an instance of this -- not some separate wiring-table type --
    // *is* the adapter. Runtime-constructed and runtime-searched for now (no
    // MATRIX/POINT-style macros yet, and no reflection) -- see the TODO in
    // core/measure.hpp for what changes here once that lands.
    //
    template<typename LocationT>
    class Adapter
    {
        public:
            Adapter( std::string_view name, std::string_view description, std::vector<AdapterPoint<LocationT>> points) :
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
            auto find( std::string_view pointName) const -> std::optional<AdapterPoint<LocationT>>
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
            std::string_view                      mName;
            std::string_view                      mDescription;
            std::vector<AdapterPoint<LocationT>>  mPoints;
    };
} // namespace core
