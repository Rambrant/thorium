#pragma once

#include "core/quantity.hpp"

namespace core
{
    using quantities::Voltage;

    //
    // Abstract instrument interfaces. These live in core so that dut and dsl can
    // reason about "an oscilloscope" or "a meter" without depending on any
    // concrete driver in hal. hal provides classes that implement these; the
    // compile-time dependency therefore points hal -> core, while at runtime
    // core/dut/dsl code calls *through* these interfaces into hal's concrete
    // implementations (dependency inversion).
    //

    //
    // Base for every instrument the rig can offer. Kept minimal on purpose --
    // capability-specific operations live in the derived interfaces below.
    //
    class IInstrument
    {
        public:
            virtual ~IInstrument() = default;
    };

    //
    // A voltmeter / DMM-style instrument: fetches a single scalar reading.
    //
    class IVoltmeter : public IInstrument
    {
        public:
            [[nodiscard]]
            virtual Voltage measureVoltage() = 0;
    };

    //
    // An oscilloscope-style instrument. For now it exposes a single scalar
    // measurement (e.g. a configured automatic measurement like Vpp/mean);
    // richer waveform capture can be layered on later without changing callers.
    //
    class IOscilloscope : public IInstrument
    {
        public:
            [[nodiscard]]
            virtual auto measureLevel() -> Voltage = 0;
    };

    // A programmable power supply: can be commanded to source a voltage.
    class IPowerSupply : public IInstrument
    {
        public:
            virtual auto setOutput( Voltage v) -> void = 0;
            virtual auto enable() -> void = 0;
            virtual auto disable() -> void = 0;
    };

}  // namespace core
