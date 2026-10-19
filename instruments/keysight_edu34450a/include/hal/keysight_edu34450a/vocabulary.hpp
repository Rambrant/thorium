#pragma once

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"
#include "core/quantities/quantity_kind.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

//
// the measurement functions
//
// Part of hal/keysight_edu34450a.hpp -- include that, not this.
//

namespace hal::keysight_edu34450a
{
    //
    // The meter's measurement functions, as its SCPI CONFigure subsystem names
    // them -- one enumerator per thing the instrument can be switched to.
    //
    // Deliberately not the same set as this class's port accessors, and the
    // two places they differ are the two places the instrument disagrees with
    // the framework. voltage() and acVoltage() return the same
    // core::Port<Voltage> and select two different functions here (the
    // difference is instrument state -- see mMode); resistance() and
    // fourWireResistance() likewise. So a port cannot be what a command is
    // built from, and this enum is what is.
    //
    // Its own scope-level enum rather than a member of EDU34450A, so that the
    // file-local command table in src/keysight_edu34450a.cpp can be a plain
    // constexpr function over it. Nothing outside this driver names it -- the
    // vocabulary a script uses is still the ports.
    //
    enum class Function
    {
        DcVoltage,
        AcVoltage,
        DcCurrent,
        AcCurrent,
        Resistance,
        FourWireResistance,
        Frequency,
        Capacitance
    };
} // namespace hal::keysight_edu34450a
