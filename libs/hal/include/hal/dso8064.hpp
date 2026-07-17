#pragma once

#include <type_traits>

#include "core/port.hpp"
#include "core/quantity.hpp"
#include "core/quantity_kind.hpp"

#include "hal/instrument.hpp"

namespace hal
{
    //
    // Agilent/Keysight Infiniium DSO8064: this rig's actual scope, replacing
    // the old generic hal::Oscilloscope placeholder (see that class's own
    // comment, still in hal/instrument.hpp, for why it existed) now that the
    // real model is known -- the same retirement hal::L4411A gave the old
    // generic hal::Dmm.
    //
    // Models the amplitude-family subset of the DSO8064's :MEASure:V...
    // automatic-measurement subsystem -- Vpp, Vmax, Vmin, Vrms, Vaverage --
    // mirroring hal::L4411A's Mode-switch pattern: calling one of the five
    // port methods below switches the instrument's current amplitude
    // measurement mode, the same way a real Infiniium's front-panel
    // Measure > Voltage menu switches which V... readout is active before a
    // reading is taken. Same accepted sharp edge as hal::L4411A's AC/DC
    // mode: a port handle read after a later mode switch reads whichever
    // mode is current at rawMeasure() time, not the mode active when the
    // handle was obtained -- harmless for Measure(port, at(...))'s
    // read-immediately-and-discard usage.
    //
    // Deliberately deferred, both pending the compile-time named-port/
    // instrument-alias design mentioned in the project's "on the horizon"
    // notes rather than another string-mode enum bolted on here:
    //   - Timing measurements (frequency, period, rise/fall time, pulse
    //     width, duty cycle) -- a second Mode-like axis, not yet added.
    //   - Channel selection -- a real DSO8064 has four physical channels,
    //     but this rig's Osc1 is wired through Matrix2 to a single
    //     crosspoint (see hal/wiring.inc), so every measurement below
    //     implicitly means "whichever channel this rig's fixed wiring puts
    //     on the scope's input", the same simplification the old
    //     hal::Oscilloscope placeholder made.
    //
    class DSO8064
    {
        public:
            enum class Mode { Vpp, Vmax, Vmin, Vrms, Vaverage };

            explicit DSO8064( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            //
            // Full swing: max - min over the acquisition, ignoring where the
            // signal sits in between -- SCPI :MEASure:VPP.
            //
            [[nodiscard]]
            auto vpp() -> core::Port<core::quantities::Voltage, DSO8064>
            {
                mMode = Mode::Vpp;
                return core::Port<core::quantities::Voltage, DSO8064>{ *this };
            }

            // SCPI :MEASure:VMAX.
            [[nodiscard]]
            auto vmax() -> core::Port<core::quantities::Voltage, DSO8064>
            {
                mMode = Mode::Vmax;
                return core::Port<core::quantities::Voltage, DSO8064>{ *this };
            }

            // SCPI :MEASure:VMIN.
            [[nodiscard]]
            auto vmin() -> core::Port<core::quantities::Voltage, DSO8064>
            {
                mMode = Mode::Vmin;
                return core::Port<core::quantities::Voltage, DSO8064>{ *this };
            }

            // True RMS over the acquisition -- SCPI :MEASure:VRMS.
            [[nodiscard]]
            auto vrms() -> core::Port<core::quantities::Voltage, DSO8064>
            {
                mMode = Mode::Vrms;
                return core::Port<core::quantities::Voltage, DSO8064>{ *this };
            }

            // Arithmetic mean over the acquisition -- SCPI :MEASure:VAVerage.
            [[nodiscard]]
            auto vaverage() -> core::Port<core::quantities::Voltage, DSO8064>
            {
                mMode = Mode::Vaverage;
                return core::Port<core::quantities::Voltage, DSO8064>{ *this };
            }

            [[nodiscard]]
            auto mode() const -> Mode
            {
                return mMode;
            }

            // Test/simulation hooks -- real hardware has no such setters.
            auto setSimulatedVpp( const core::quantities::Voltage v) -> void
            {
                mSimVpp = v;
            }

            auto setSimulatedVmax( const core::quantities::Voltage v) -> void
            {
                mSimVmax = v;
            }

            auto setSimulatedVmin( const core::quantities::Voltage v) -> void
            {
                mSimVmin = v;
            }

            auto setSimulatedVrms( const core::quantities::Voltage v) -> void
            {
                mSimVrms = v;
            }

            auto setSimulatedVaverage( const core::quantities::Voltage v) -> void
            {
                mSimVaverage = v;
            }

            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & ) -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    switch( mMode)
                    {
                        case Mode::Vpp:      return mSimVpp;
                        case Mode::Vmax:     return mSimVmax;
                        case Mode::Vmin:     return mSimVmin;
                        case Mode::Vrms:     return mSimVrms;
                        case Mode::Vaverage: return mSimVaverage;
                    }

                    return mSimVpp;
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "DSO8064 has no port for this quantity");
                }
            }

        private:
            InstrumentId               mId;
            Mode                       mMode{ Mode::Vpp };
            core::quantities::Voltage  mSimVpp{};
            core::quantities::Voltage  mSimVmax{};
            core::quantities::Voltage  mSimVmin{};
            core::quantities::Voltage  mSimVrms{};
            core::quantities::Voltage  mSimVaverage{};
    };
} // namespace hal
