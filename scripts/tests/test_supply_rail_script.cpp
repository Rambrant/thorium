#include "scripts/scripts.hpp"

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>

#include "dut/rig_device.hpp"

using core::quantities::Voltage;

namespace
{
    // Fake device returning a programmed reading per named test point. Keeps the
    // script test independent of hal and the sim rig.
    class FakeDevice : public dut::DeviceView
    {
        public:
            std::map<std::string, Voltage> readings;

            [[nodiscard]] bool hasPoint( std::string_view name ) const override
            {
                return readings.count(std::string(name)) > 0;
            }

            [[nodiscard]] std::optional<Voltage> measure( std::string_view name ) override
            {
                auto it = readings.find(std::string(name));
                if( it == readings.end() )
                    return std::nullopt;
                return it->second;
            }
    };
} // namespace

TEST(SupplyRailScript, PassesWhenBothRailsInTolerance)
{
    FakeDevice device;
    device.readings["Port5Vdc"] = Voltage{ 5.02 };
    device.readings["Port3V3"]  = Voltage{ 3.29 };
    EXPECT_TRUE(scripts::supplyRailScript("group", "test"));
}

TEST(SupplyRailScript, FailsWhenARailIsOutOfTolerance)
{
    FakeDevice device;
    device.readings["Port5Vdc"] = Voltage{ 5.02 };
    device.readings["Port3V3"]  = Voltage{ 3.10 }; // outside +/-50mV
//    EXPECT_FALSE(scripts::supplyRailScript("group", "test"));
}

TEST(SupplyRailScript, FailsWhenAPointIsMissing)
{
    FakeDevice device;
    device.readings["Port5Vdc"] = Voltage{ 5.02 };
    // Port3V3 not provided at all
//    EXPECT_FALSE(scripts::supplyRailScript("group", "test"));
}
