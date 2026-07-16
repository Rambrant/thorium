#pragma once

#include <string_view>
#include <type_traits>

#include "core/port.hpp"
#include "core/quantity.hpp"
#include "core/quantity_kind.hpp"

namespace hal
{
    //
    // The fixed, static list of instruments this rig actually has. Several
    // instruments can share the same underlying type (e.g. Dmm1 and Dmm2
    // below are both hal::Dmm) and be interchanged during a test -- InstrumentId
    // is what a hal::RouteTable entry and a recorded sample identify the
    // instrument by, since two Dmms are two distinct wiring facts even though
    // they're the same C++ type.
    //
    // PowerSupply1/PowerSupply has been retired in favor of DcP1..DcP4/AcP1
    // below -- concrete, named source instruments with a real
    // Apply(...)/Remove(...) builder (see hal/n6701a.hpp, hal/ac6677a.hpp)
    // rather than the old ad-hoc setOutput()/enable() pair. DcP1..DcP4 name
    // the role ("DC power, channel N"), not the model -- the four channels
    // of one physical N6701A mainframe -- the same way Dmm1/Dmm2 don't
    // encode which literal DMM model they are; see hal::N6701A's own
    // comment in hal/n6701a.hpp for why the class itself is model-named
    // even though these ids aren't.
    //
    enum class InstrumentId
    {
        Osc1,
        Dmm1,
        Dmm2,
        DcP1,
        DcP2,
        DcP3,
        DcP4,
        AcP1
    };

    [[nodiscard]]
    auto to_string( InstrumentId id) -> std::string_view;

    //
    // An oscilloscope: a single Voltage-measuring port (an automatic
    // measurement such as Vpp/mean; richer waveform capture can be layered on
    // later without changing callers). mSimVoltage stands in for the real
    // VXI/GPIB driver read until one exists.
    //
    class Oscilloscope
    {
        public:
            explicit Oscilloscope( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto voltage() -> core::Port<core::quantities::Voltage, Oscilloscope>
            {
                return core::Port<core::quantities::Voltage, Oscilloscope>{ *this };
            }

            // Test/simulation hook -- real hardware has no such setter.
            auto setSimulatedVoltage( const core::quantities::Voltage v) -> void
            {
                mSimVoltage = v;
            }

            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & ) -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mSimVoltage;
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "Oscilloscope has no port for this quantity");
                }
            }

        private:
            InstrumentId              mId;
            core::quantities::Voltage mSimVoltage{};
    };

    //
    // A DMM: measures both Voltage and Current, each its own port, in either
    // DC or AC mode. AC mode is deliberately stored on the instrument, not
    // the port: calling .acVoltage()/.acCurrent() switches this Dmm into AC
    // mode, and .voltage()/.current() switch it back to DC, exactly the same
    // way a physical bench DMM's front-panel mode button works. This has a
    // known, accepted sharp edge -- two port handles obtained before/after a
    // mode switch both read whatever mode is *currently* set when
    // rawMeasure() is eventually called, not the mode active when each port
    // was created -- but that never matters for real usage, since
    // Measure(port, at(...)) reads a port immediately and discards it. If
    // this ever bites in practice, the fix is a DmmChannel<QuantityT, Mode>
    // per-mode port type; sketched and set aside as overengineering for now.
    //
    class Dmm
    {
        public:
            enum class Mode { Dc, Ac };

            explicit Dmm( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto voltage() -> core::Port<core::quantities::Voltage, Dmm>
            {
                mMode = Mode::Dc;
                return core::Port<core::quantities::Voltage, Dmm>{ *this };
            }

            [[nodiscard]]
            auto current() -> core::Port<core::quantities::Current, Dmm>
            {
                mMode = Mode::Dc;
                return core::Port<core::quantities::Current, Dmm>{ *this };
            }

            [[nodiscard]]
            auto acVoltage() -> core::Port<core::quantities::Voltage, Dmm>
            {
                mMode = Mode::Ac;
                return core::Port<core::quantities::Voltage, Dmm>{ *this };
            }

            [[nodiscard]]
            auto acCurrent() -> core::Port<core::quantities::Current, Dmm>
            {
                mMode = Mode::Ac;
                return core::Port<core::quantities::Current, Dmm>{ *this };
            }

            [[nodiscard]]
            auto mode() const -> Mode
            {
                return mMode;
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
                else
                {
                    static_assert( !sizeof( QuantityT), "Dmm has no port for this quantity");
                }
            }

        private:
            InstrumentId              mId;
            Mode                      mMode{ Mode::Dc };
            core::quantities::Voltage mSimVoltage{};
            core::quantities::Current mSimCurrent{};
            core::quantities::Voltage mSimAcVoltage{};
            core::quantities::Current mSimAcCurrent{};
    };
} // namespace hal
