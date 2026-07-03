#pragma once

#include <optional>
#include <string_view>

#include "core/quantity.hpp"
#include "core/rig.hpp"
#include "dut/matrix_map.hpp"

namespace dut {

// A DUT modelled in terms of rig instruments and matrix routing, rather than
// a register bus. The device governs *what* is needed to test it: it holds
// the routing table (which logical test point uses which instrument and
// crosspoint) and, given a rig, performs the select-instrument + close-
// crosspoint steps. Fetching the actual reading is exposed as a primitive
// the dsl layer drives.
//
// RigDevice is templated on the matrix map size so the routing table stays a
// compile-time constant; DeviceView below erases that so callers (dsl) don't
// need the size.

// Type-erased handle the dsl layer measures through: it can look up a test
// point by name and route + read it, without knowing the map's size.
class DeviceView {
public:
    virtual ~DeviceView() = default;

    // Route the matrix for the named test point and take a reading from its
    // instrument. Returns nullopt if the point is unknown or its instrument
    // isn't present on the rig.
    [[nodiscard]] virtual std::optional<core::Voltage> measure(std::string_view point_name) = 0;

    [[nodiscard]] virtual bool has_point(std::string_view point_name) const = 0;
};

template <std::size_t N>
class RigDevice : public DeviceView {
public:
    RigDevice(core::IRig& rig, const MatrixMap<N>& map) : rig_(rig), map_(map) {}

    [[nodiscard]] bool has_point(std::string_view point_name) const override {
        return map_.find(point_name).has_value();
    }

    [[nodiscard]] std::optional<core::Voltage> measure(std::string_view point_name) override {
        const auto point = map_.find(point_name);
        if (!point) {
            return std::nullopt;
        }

        // Establish the connection: close this point's crosspoint. We open
        // the matrix first so only the intended path is live.
        rig_.matrix().open_all();
        rig_.matrix().close(point->crosspoint);

        // Select the instrument abstractly and fetch. The rig hands back a
        // core interface; we never see the concrete driver.
        switch (point->instrument) {
            case Instrument::Voltmeter:
                if (auto* vm = rig_.voltmeter()) {
                    return vm->measure_voltage();
                }
                return std::nullopt;
            case Instrument::Oscilloscope:
                if (auto* scope = rig_.oscilloscope()) {
                    return scope->measure_level();
                }
                return std::nullopt;
            case Instrument::PowerSupply:
                // A power supply sources rather than measures; not a valid
                // measurement target.
                return std::nullopt;
        }
        return std::nullopt;
    }

private:
    core::IRig& rig_;
    const MatrixMap<N>& map_;
};

// Deduction guide so `RigDevice dev{rig, StdAdapter};` picks up N.
template <std::size_t N>
RigDevice(core::IRig&, const MatrixMap<N>&) -> RigDevice<N>;

}  // namespace dut
