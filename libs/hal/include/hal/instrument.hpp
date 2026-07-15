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
    enum class InstrumentId
    {
        Osc1,
        Dmm1,
        Dmm2,
        PowerSupply1
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
            auto rawMeasure() -> QuantityT
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
    // A DMM: measures both Voltage and Current, each its own port.
    //
    class Dmm
    {
        public:
            explicit Dmm( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto voltage() -> core::Port<core::quantities::Voltage, Dmm>
            {
                return core::Port<core::quantities::Voltage, Dmm>{ *this };
            }

            [[nodiscard]]
            auto current() -> core::Port<core::quantities::Current, Dmm>
            {
                return core::Port<core::quantities::Current, Dmm>{ *this };
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

            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure() -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mSimVoltage;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                {
                    return mSimCurrent;
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "Dmm has no port for this quantity");
                }
            }

        private:
            InstrumentId              mId;
            core::quantities::Voltage mSimVoltage{};
            core::quantities::Current mSimCurrent{};
    };

    //
    // A programmable power supply: sources rather than measures, so it has no
    // Measure-side port today. Kept concrete (rather than left out) because
    // it's a real, fixed member of the instrument list -- the Setup side of
    // this same builder pattern (dut's "eventually Setup uses" this) will add
    // its own typed port (e.g. .voltageOutput()) once that design is settled.
    //
    class PowerSupply
    {
        public:
            explicit PowerSupply( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            auto setOutput( const core::quantities::Voltage v) -> void
            {
                mOutput = v;
            }

            auto enable() -> void
            {
                mEnabled = true;
            }

            auto disable() -> void
            {
                mEnabled = false;
            }

            [[nodiscard]]
            auto isEnabled() const -> bool
            {
                return mEnabled;
            }

            [[nodiscard]]
            auto output() const -> core::quantities::Voltage
            {
                return mOutput;
            }

        private:
            InstrumentId              mId;
            core::quantities::Voltage mOutput{};
            bool                      mEnabled{ false };
    };
} // namespace hal
