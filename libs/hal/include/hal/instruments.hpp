#pragma once

#include <map>

#include "core/instrument.hpp"
#include "core/switch_matrix.hpp"

namespace hal {

// Concrete, simulated instruments. On a real rig these would drive actual
// hardware (VISA/SCPI, vendor SDKs, etc.); here they return programmable
// canned values so the whole stack is testable without a bench. Each
// implements a core:: interface, so nothing above hal names these types.

// A voltmeter whose reading depends on which matrix crosspoint is currently
// routed to it -- mimicking real hardware, where the value you read reflects
// whatever the matrix has connected. Falls back to a default reading for any
// crosspoint not explicitly programmed.
class SimVoltmeter : public core::IVoltmeter {
public:
    explicit SimVoltmeter(core::Voltage default_reading) : default_(default_reading) {}

    void set_reading(core::Voltage v) { default_ = v; }

    // Program the reading seen when a specific crosspoint is the live route.
    void set_reading_at(core::Crosspoint point, core::Voltage v) {
        by_point_[key(point)] = v;
    }

    // Called by the rig to tell the instrument which crosspoint is live.
    void set_active_crosspoint(core::Crosspoint point) { active_ = point; }

    [[nodiscard]] core::Voltage measure_voltage() override {
        auto it = by_point_.find(key(active_));
        return it != by_point_.end() ? it->second : default_;
    }

private:
    [[nodiscard]] static std::uint32_t key(core::Crosspoint p) {
        return (static_cast<std::uint32_t>(p.row) << 16) | p.col;
    }

    core::Voltage default_;
    core::Crosspoint active_{0xFFFF, 0xFFFF};
    std::map<std::uint32_t, core::Voltage> by_point_;
};

class SimOscilloscope : public core::IOscilloscope {
public:
    explicit SimOscilloscope(core::Voltage level) : level_(level) {}

    void set_level(core::Voltage v) { level_ = v; }

    [[nodiscard]] core::Voltage measure_level() override { return level_; }

private:
    core::Voltage level_;
};

class SimPowerSupply : public core::IPowerSupply {
public:
    void set_output(core::Voltage v) override { output_ = v; }
    void enable() override { enabled_ = true; }
    void disable() override { enabled_ = false; }

    [[nodiscard]] core::Voltage output() const { return output_; }
    [[nodiscard]] bool enabled() const { return enabled_; }

private:
    core::Voltage output_{0.0};
    bool enabled_{false};
};

}  // namespace hal
