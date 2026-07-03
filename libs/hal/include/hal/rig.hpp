#pragma once

#include "core/rig.hpp"
#include "hal/instruments.hpp"
#include "hal/switch_matrix.hpp"

namespace hal {

// A concrete rig wiring together simulated instruments and a switch matrix.
// This is the "collection of instruments" the physical bench owns; many
// different DUTs are tested against the same rig. Construction of the real
// thing (which instruments exist, matrix size) is a fixed rig property.
class SimRig : public core::IRig {
public:
    SimRig()
        : matrix_(*this, /*rows=*/8, /*cols=*/8),
          scope_(core::Voltage{0.0}),
          voltmeter_(core::Voltage{0.0}) {}

    [[nodiscard]] core::IOscilloscope* oscilloscope() override { return &scope_; }
    [[nodiscard]] core::IVoltmeter* voltmeter() override { return &voltmeter_; }
    [[nodiscard]] core::IPowerSupply* powerSupply() override { return &supply_; }
    [[nodiscard]] core::ISwitchMatrix& matrix() override { return matrix_; }

    // Direct access to the concrete instruments, for tests/harness setup that
    // needs to program canned readings. Code above hal never uses these.
    [[nodiscard]] SimOscilloscope& sim_scope() { return scope_; }
    [[nodiscard]] SimVoltmeter& sim_voltmeter() { return voltmeter_; }
    [[nodiscard]] SimPowerSupply& sim_supply() { return supply_; }

private:
    // A matrix wrapper that, on close(), tells the simulated instruments
    // which crosspoint is now live -- so a SimVoltmeter programmed per
    // crosspoint returns the right value. On real hardware this coupling is
    // physical; here we model it explicitly.
    class NotifyingMatrix : public SwitchMatrix {
    public:
        NotifyingMatrix(SimRig& rig, std::uint16_t rows, std::uint16_t cols)
            : SwitchMatrix(rows, cols), rig_(rig) {}

        void close(core::Crosspoint point) override {
            SwitchMatrix::close(point);
            rig_.voltmeter_.set_active_crosspoint(point);
        }

    private:
        SimRig& rig_;
    };

    NotifyingMatrix matrix_;
    SimOscilloscope scope_;
    SimVoltmeter voltmeter_;
    SimPowerSupply supply_;
};

}  // namespace hal
