#include "dsl/measure.hpp"

#include <gtest/gtest.h>

#include <optional>

#include "dsl/criterion.hpp"
#include "dsl/predicates.hpp"

namespace {

// Fake device: proves Measure depends only on dut::DeviceView, not on any
// rig or instrument. Returns a programmed reading for a known point.
class FakeDevice : public dut::DeviceView {
public:
    std::optional<core::Voltage> reading;
    std::string_view last_point;

    [[nodiscard]] bool has_point(std::string_view name) const override {
        return name == "5Vdc_port";
    }

    [[nodiscard]] std::optional<core::Voltage> measure(std::string_view name) override {
        last_point = name;
        if (name == "5Vdc_port") {
            return reading;
        }
        return std::nullopt;
    }
};

}  // namespace

GROUP(FS_Supply, "Supply rail checks")
    CRIT(FS_Vout_01, dsl::EQ(5.0).within(0.05), "5Vdc rail within tolerance")
END_GROUP

TEST(DslMeasure, PassesWhenReadingMeetsCriterion) {
    FakeDevice device;
    device.reading = core::Voltage{5.02};
    EXPECT_TRUE(dsl::Measure(device, FS_Supply::FS_Vout_01, "5Vdc_port"));
    EXPECT_EQ(device.last_point, "5Vdc_port");
}

TEST(DslMeasure, FailsWhenReadingOutsideTolerance) {
    FakeDevice device;
    device.reading = core::Voltage{5.20};
    EXPECT_FALSE(dsl::Measure(device, FS_Supply::FS_Vout_01, "5Vdc_port"));
}

TEST(DslMeasure, FailsWhenTestPointUnknown) {
    FakeDevice device;
    device.reading = core::Voltage{5.0};
    EXPECT_FALSE(dsl::Measure(device, FS_Supply::FS_Vout_01, "unknown_point"));
}

TEST(DslMeasure, FailsWhenInstrumentReturnsNothing) {
    FakeDevice device;
    device.reading = std::nullopt;  // instrument present but no reading
    EXPECT_FALSE(dsl::Measure(device, FS_Supply::FS_Vout_01, "5Vdc_port"));
}
