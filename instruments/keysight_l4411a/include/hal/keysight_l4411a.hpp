#pragma once

#include <type_traits>

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"
#include "core/quantities/quantity_kind.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/api_version.hpp"
#include "hal/driver/instrument.hpp"

//
// Which hal API version this driver was written against. A driver and the hal
// it is compiled against are distributed separately -- this directory is meant
// to be zipped and dropped into another rig's instruments/ as it stands (see
// instruments/README.md) -- so the two can disagree, and this line is what
// makes that disagreement one readable diagnostic instead of a failure deep
// inside a template instantiation. A literal, never THORIUM_HAL_API_VERSION
// itself, which would only assert that this hal matches this hal. See
// hal/driver/api_version.hpp for what the number means and when it moves.
//
THORIUM_REQUIRE_HAL_API( 1);

//
// This driver's own namespace, nested inside hal.
//
// Everything a driver declares used to land directly in hal, and two problems
// followed. Satellite types had to invent long unique names to stay out of each
// other's way -- L4411A and its siblings below carried this
// model's own name purely for that -- and the ones that did not, because
// nothing had claimed the word yet, ended up owning names no single instrument
// has any business owning. A namespace of its own retires both: the prefixes
// are gone from the names below, and what is left cannot collide with another
// driver at all.
//
// The name carries the manufacturer as well as the model, because a model
// number is not unique either -- two vendors may both ship a "1260" or a
// "4411". Here the L4411A shipped as an Agilent product and is now Keysight; the
// token follows the company as it is today rather than the badge on the front
// panel, so that one vendor does not appear under two spellings.
//
// Nested inside hal rather than beside it, which is what keeps the bodies below
// unchanged: unqualified lookup still reaches hal::describeSetting,
// hal::InstrumentId, hal::Simulated and the rest of the driver kit from in
// here. The ADL customization points are now found in *this* namespace on this
// driver's own config types, rather than as one more overload in a pile shared
// with every other instrument.
//
namespace hal::keysight_l4411a
{
    //
    // Agilent/Keysight L4411A: the 1U, half-rack LXI version of the 34411A
    // 6.5-digit DMM, sharing that model's SCPI dialect and measurement
    // functions exactly (see the instrument's own Getting Started Guide) --
    // this class models that shared function set, not anything specific to
    // the L4411A's LXI packaging. A rig's Dmm1/Dmm2 (see, in this repo,
    // rig/instrument.inc) are two such instances, exactly the way
    // DcP1..DcP4 are four hal::keysight_n6701a::N6701A instances -- two distinct wiring
    // facts sharing one C++ type.
    //
    // This retires the old generic hal::Dmm placeholder the same way
    // hal::keysight_n6701a::N6701A/hal::keysight_ac6834b::Ac6834B retired the old generic PowerSupply: once the
    // real model is known, naming the class after it documents the
    // non-portability of its measurement-function set (CONF:VOLT:DC,
    // CONF:RES, CONF:FRES, ...) rather than pretending a DMM driver is as
    // interchangeable across models as a generic placeholder can pretend to
    // be -- hal::keysight_dso8064a::DSO8064A (see hal/keysight_dso8064a.hpp) has since given
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

            //
            // LAN or USB, and nothing else: that is what the LXI packaging
            // this model *is* (see this class's own comment) amounts to on
            // the back panel -- no GPIB connector, unlike the rack-mount
            // 34411A it otherwise shares everything with. So a rig row
            // addressing one of its Dmms over GPIB fails to compile rather
            // than failing to open, see hal::ReachableOver in hal/driver/address.hpp
            // -- which is also where the reasoning lives for why the bus
            // *kind* is checked here and the address itself is just a value
            // this driver carries.
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Lan, Usb>
            L4411A( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}

            //
            // Where the PC reaches this meter -- nothing reads it yet, the
            // same way hal::keysight_n6701a::N6701A's mainframe slot was carried before any
            // driver needed it, because the alternative is a rig table that
            // cannot say the thing at all.
            //
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mAddress;
            }

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
            // requiresSensePath() (see core/driver/port.hpp) is what actually
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
            // hal::safeRig() (see hal/verbs/safing.hpp) to drop. Its leads are
            // taken off the DUT by the fabric's own openAll(), which
            // safeRig() does after this, not by anything this instrument
            // is asked to do.
            //
            // Written out as an explicit empty body rather than left off
            // and detected as absent, on purpose. safeRig() calls safe()
            // on every instrument in the rig's instrument.inc
            // unconditionally, so a driver that lacks it fails to compile
            // -- see
            // hal::SafeableInstrument in hal/driver/instrument.hpp. Had safing
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
            Address                       mAddress;
            Mode                          mMode{ Mode::Dc };
            ResistanceMode                mResistanceMode{ ResistanceMode::TwoWire };
            core::quantities::Voltage     mSimVoltage{};
            core::quantities::Current     mSimCurrent{};
            core::quantities::Voltage     mSimAcVoltage{};
            core::quantities::Current     mSimAcCurrent{};
            core::quantities::Resistance  mSimResistance{};
            core::quantities::Resistance  mSimFourWireResistance{};
    };
} // namespace hal::keysight_l4411a
