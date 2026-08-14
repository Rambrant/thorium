#pragma once

#include <type_traits>

#include "core/port.hpp"
#include "core/quantity.hpp"
#include "core/quantity_kind.hpp"

#include "hal/instrument.hpp"

namespace hal
{
    //
    // Agilent/Keysight L4411A: the 1U, half-rack LXI version of the 34411A
    // 6.5-digit DMM, sharing that model's SCPI dialect and measurement
    // functions exactly (see the instrument's own Getting Started Guide) --
    // this class models that shared function set, not anything specific to
    // the L4411A's LXI packaging. A rig's Dmm1/Dmm2 (see, in this repo,
    // rig/instrument.inc) are two such instances, exactly the way
    // DcP1..DcP4 are four hal::N6701A instances -- two distinct wiring
    // facts sharing one C++ type.
    //
    // This retires the old generic hal::Dmm placeholder the same way
    // hal::N6701A/hal::Ac6677A retired the old generic PowerSupply: once the
    // real model is known, naming the class after it documents the
    // non-portability of its measurement-function set (CONF:VOLT:DC,
    // CONF:RES, CONF:FRES, ...) rather than pretending a DMM driver is as
    // interchangeable across models as a generic placeholder can pretend to
    // be -- hal::DSO8064 (see hal/dso8064.hpp) has since given
    // hal::Oscilloscope the same retirement.
    //
    // AC/DC mode and 2-wire/4-wire resistance mode are both deliberately
    // stored on the instrument, not the port -- calling .acVoltage()/
    // .acCurrent() switches this Dmm into AC mode, .voltage()/.current()
    // switch it back to DC, and .resistance()/.fourWireResistance() switch
    // between 2-wire and 4-wire ohms, exactly the way a physical L4411A's
    // front-panel function keys work. This has the same known, accepted
    // sharp edge hal::Dmm's Mode always had -- a port handle obtained
    // before/after a mode switch reads whatever mode is *currently* set
    // when rawMeasure() is eventually called, not the mode active when the
    // handle was created -- but that never matters for real usage, since
    // Measure(port, at(...)) reads a port immediately and discards it.
    //
    class L4411A : public InstrumentTag
    {
        public:
            enum class Mode           { Dc, Ac };
            enum class ResistanceMode { TwoWire, FourWire };

            explicit L4411A( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto voltage() -> core::Port<core::quantities::Voltage, L4411A>
            {
                mMode = Mode::Dc;
                return core::Port<core::quantities::Voltage, L4411A>{ *this };
            }

            [[nodiscard]]
            auto current() -> core::Port<core::quantities::Current, L4411A>
            {
                mMode = Mode::Dc;
                return core::Port<core::quantities::Current, L4411A>{ *this };
            }

            [[nodiscard]]
            auto acVoltage() -> core::Port<core::quantities::Voltage, L4411A>
            {
                mMode = Mode::Ac;
                return core::Port<core::quantities::Voltage, L4411A>{ *this };
            }

            [[nodiscard]]
            auto acCurrent() -> core::Port<core::quantities::Current, L4411A>
            {
                mMode = Mode::Ac;
                return core::Port<core::quantities::Current, L4411A>{ *this };
            }

            //
            // 2-wire ohms: the meter's own lead resistance is included in
            // the reading, same as pressing the front-panel 2W OHMS key.
            //
            [[nodiscard]]
            auto resistance() -> core::Port<core::quantities::Resistance, L4411A>
            {
                mResistanceMode = ResistanceMode::TwoWire;
                return core::Port<core::quantities::Resistance, L4411A>{ *this };
            }

            //
            // 4-wire (Kelvin) ohms: separate sense leads cancel out lead
            // resistance, same as pressing the front-panel 4W OHMS key --
            // requiresSensePath() (see core/port.hpp) is what actually
            // tells core::MeasureEngine to route this instrument's sense
            // channel and the DUT point's sense channel alongside the
            // normal force path, only for this one reading. A plain
            // resistance() call above never sets it, so a 2-wire reading
            // on this same instrument never touches sense at all.
            //
            [[nodiscard]]
            auto fourWireResistance() -> core::Port<core::quantities::Resistance, L4411A, core::SensePath::Required>
            {
                mResistanceMode = ResistanceMode::FourWire;
                return core::Port<core::quantities::Resistance, L4411A>{ *this }.requiresSensePath();
            }

            [[nodiscard]]
            auto mode() const -> Mode
            {
                return mMode;
            }

            //
            // Nothing to do, and that is a hardware fact rather than an
            // unfinished implementation: a DMM is a passive instrument --
            // it sources nothing into the DUT, so there is no output for
            // hal::safeRig() (see hal/safing.hpp) to drop. Its leads are
            // taken off the DUT by the fabric's own openAll(), which
            // safeRig() does after this, not by anything this instrument
            // is asked to do.
            //
            // Written out as an explicit empty body rather than left off
            // and detected as absent, on purpose. safeRig() calls safe()
            // on every instrument in the rig's instrument.inc
            // unconditionally, so a driver that lacks it fails to compile
            // -- see
            // hal::SafeableInstrument in hal/instrument.hpp. Had safing
            // instead been opt-in (an ADL customization point in the shape
            // of applyDriver/connectDriver, say, defaulting to a no-op for
            // anything that doesn't provide one), a *source* instrument
            // whose author forgot to write safe() would silently never be
            // safed. A silent omission on the safing path is the one
            // failure mode this whole mechanism exists to rule out, so the
            // cost is paid here instead: two passive drivers each carry
            // one deliberately empty function.
            //
            auto safe() -> void
            {
            }

            [[nodiscard]]
            auto resistanceMode() const -> ResistanceMode
            {
                return mResistanceMode;
            }

            // Test/simulation hooks -- real hardware has no such setters.
            auto setSimulatedVoltage( const core::quantities::Voltage v) -> void
            {
                mSimVoltage = v;
            }

            auto setSimulatedCurrent( const core::quantities::Current c) -> void
            {
                mSimCurrent = c;
            }

            auto setSimulatedAcVoltage( const core::quantities::Voltage v) -> void
            {
                mSimAcVoltage = v;
            }

            auto setSimulatedAcCurrent( const core::quantities::Current c) -> void
            {
                mSimAcCurrent = c;
            }

            auto setSimulatedResistance( const core::quantities::Resistance r) -> void
            {
                mSimResistance = r;
            }

            auto setSimulatedFourWireResistance( const core::quantities::Resistance r) -> void
            {
                mSimFourWireResistance = r;
            }

            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & ) -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mMode == Mode::Ac ? mSimAcVoltage : mSimVoltage;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                {
                    return mMode == Mode::Ac ? mSimAcCurrent : mSimCurrent;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Resistance>)
                {
                    return mResistanceMode == ResistanceMode::FourWire ? mSimFourWireResistance : mSimResistance;
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "L4411A has no port for this quantity");
                }
            }

        private:
            InstrumentId                  mId;
            Mode                          mMode{ Mode::Dc };
            ResistanceMode                mResistanceMode{ ResistanceMode::TwoWire };
            core::quantities::Voltage     mSimVoltage{};
            core::quantities::Current     mSimCurrent{};
            core::quantities::Voltage     mSimAcVoltage{};
            core::quantities::Current     mSimAcCurrent{};
            core::quantities::Resistance  mSimResistance{};
            core::quantities::Resistance  mSimFourWireResistance{};
    };
} // namespace hal
