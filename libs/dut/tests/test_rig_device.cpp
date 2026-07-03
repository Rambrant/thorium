#include "dut/rig_device.hpp"

#include <gtest/gtest.h>

#include "core/rig.hpp"
#include "dut/matrix_map.hpp"

namespace {

// A hand-rolled fake rig -- proves RigDevice depends only on core::IRig and
// can be tested with zero hal involvement.
class FakeRig : public core::IRig {
public:
    class FakeMatrix : public core::ISwitchMatrix {
    public:
        void close(core::Crosspoint p) override { last_closed = p; ++close_count; }
        void open(core::Crosspoint) override {}
        void openAll() override { ++open_all_count; }
        [[nodiscard]] bool isClosed(core::Crosspoint p) const override { return p == last_closed; }

        core::Crosspoint last_closed{0xFFFF, 0xFFFF};
        int close_count{0};
        int open_all_count{0};
    };

    class FakeVoltmeter : public core::IVoltmeter {
    public:
        core::Voltage reading{0.0};
        [[nodiscard]] core::Voltage measureVoltage() override { return reading; }
    };

    class FakeScope : public core::IOscilloscope {
    public:
        core::Voltage level{0.0};
        [[nodiscard]] core::Voltage measureLevel() override { return level; }
    };

    [[nodiscard]] core::IOscilloscope* oscilloscope() override { return &scope; }
    [[nodiscard]] core::IVoltmeter* voltmeter() override { return &vm; }
    [[nodiscard]] core::IPowerSupply* powerSupply() override { return nullptr; }
    [[nodiscard]] core::ISwitchMatrix& matrix() override { return mtx; }

    FakeMatrix mtx;
    FakeVoltmeter vm;
    FakeScope scope;
};

}  // namespace

MATRIX(RigDevTestAdapter, "Adapter for RigDevice tests")
    POINT(Port5Vdc, Instrument::Voltmeter, 3, 7, "5Vdc supply port")
    POINT(ClkProbe, Instrument::Oscilloscope, 1, 2, "Config clock probe")
END_MATRIX

TEST(DutRigDevice, MeasureRoutesMatrixThenReadsVoltmeter) {
    FakeRig rig;
    rig.vm.reading = core::Voltage{5.01};
    dut::RigDevice device{rig, RigDevTestAdapter};

    auto result = device.measure("Port5Vdc");

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->value(), 5.01);
    // Crosspoint for Port5Vdc is (3,7) -- confirm the matrix was routed there.
    EXPECT_EQ(rig.mtx.last_closed, (core::Crosspoint{3, 7}));
    // Matrix should be cleared before closing the intended path.
    EXPECT_EQ(rig.mtx.open_all_count, 1);
}

TEST(DutRigDevice, MeasureUsesOscilloscopeForScopePoints) {
    FakeRig rig;
    rig.scope.level = core::Voltage{3.28};
    dut::RigDevice device{rig, RigDevTestAdapter};

    auto result = device.measure("ClkProbe");

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->value(), 3.28);
    EXPECT_EQ(rig.mtx.last_closed, (core::Crosspoint{1, 2}));
}

TEST(DutRigDevice, UnknownPointReturnsNullopt) {
    FakeRig rig;
    dut::RigDevice device{rig, RigDevTestAdapter};
    EXPECT_FALSE(device.measure("nonexistent").has_value());
}

TEST(DutRigDevice, HasPointReflectsMap) {
    FakeRig rig;
    dut::RigDevice device{rig, RigDevTestAdapter};
    EXPECT_TRUE(device.hasPoint("Port5Vdc"));
    EXPECT_FALSE(device.hasPoint("nope"));
}
