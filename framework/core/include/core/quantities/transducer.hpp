#pragma once

#include <string_view>
#include <type_traits>

#include "core/quantities/quantity.hpp"

namespace core
{
    namespace quantities
    {
        //
        // A named conversion from the quantity an instrument actually measured
        // to the quantity the DUT was really being asked about.
        //
        // ---------------------------------------------------------------------
        // What this is for
        // ---------------------------------------------------------------------
        //
        // A sensor on the device answers in the wrong unit on purpose. A
        // thermistor divider or an LM35 turns a temperature into a voltage
        // precisely so that a voltmeter can read it -- so the bench measures
        // volts, the criterion is written in degrees, and something has to
        // stand between the two. Without this, that something is a bare
        // `Temperature{ reading.value() * 100.0 }` at a call site: a magic
        // number, in a unit-shaped cast, with the sensor it belongs to recorded
        // nowhere.
        //
        // The conversion is a value here rather than an operator on Quantity
        // (see quantity.hpp's own note on the Temperature alias, which is the
        // long form of this argument). Ohm's law is in the algebra because it
        // is true of every resistor; 10 mV/degC is true of one part number, and
        // an operator would make the wrong sensor's curve a silent success
        // instead of a compile error.
        //
        // ---------------------------------------------------------------------
        // Where the safety is
        // ---------------------------------------------------------------------
        //
        // In the signature, entirely. A Transducer<Voltage, Temperature> takes
        // a Voltage and nothing else -- point it at a Current reading, or at
        // the Temperature it produced, and it does not compile. Two sensors on
        // one adapter cannot be swapped for each other unless they genuinely
        // sense and derive the same units, and even then they are two named
        // values a reader can tell apart, which two lambdas at two call sites
        // are not.
        //
        // Note what it deliberately does NOT do: forbid measuring anything else
        // at that pin. A transducer is declared alongside a POINT, never inside
        // its AdapterPointTag -- core/topology/adapter.hpp already argues at
        // length why a point that declares its quantity forbids ordinary,
        // correct measurements. That a sensor is fitted to a pin is a fact
        // about the DUT; it is not a claim that the pin has no other uses.
        //
        // ---------------------------------------------------------------------
        // Where the conversion belongs in a run
        // ---------------------------------------------------------------------
        //
        // After the session has answered, never before it. Measure() records
        // what the instrument returned, so a sensed point stays keyed as a
        // Voltage in the recording and a replayed run derives the temperature
        // from those same volts. Recording the derived value instead would bake
        // one calibration permanently into the archive, and a recalibrated
        // sensor would have no way to correct yesterday's runs.
        //
        //     const auto sense   = Measure( Dmm1.voltage(), at( dut::CoolantSense));
        //     const auto coolant = dut::CoolantSensor( sense);
        //
        //     Verify( FS_Thermal_1::FS_Coolant_Max, coolant);
        //
        // Two statements rather than one on purpose: both units stay visible at
        // the call site, so the script says the bench read a voltage and the
        // device is at a temperature, which is what actually happened.
        //
        // ---------------------------------------------------------------------
        // The transfer function
        // ---------------------------------------------------------------------
        //
        // A plain function pointer, not a std::function. It keeps a Transducer
        // usable in a constant expression -- so a sensor can be a constexpr
        // value declared beside the adapter table rather than something
        // constructed at startup -- and it costs nothing at the call site,
        // since a captureless lambda converts to one with a leading +.
        //
        // Linearity is not assumed anywhere: the function is opaque, so a
        // Steinhart-Hart NTC curve or a thermocouple polynomial is simply a
        // longer body. Nor is any range checking done here. Like Quantity
        // itself, this is a plain value mapping -- a reading outside the
        // sensor's valid span produces whatever the curve says, and it is a
        // criterion's job to fail it.
        //
        template<QuantityType SensedT, QuantityType DerivedT>
        class Transducer
        {
            public:
                using Sensed  = SensedT;
                using Derived = DerivedT;

                constexpr Transducer( const std::string_view name,
                                      const std::string_view description,
                                      DerivedT ( * const transfer)( SensedT)) :
                    mName( name),
                    mDescription( description),
                    mTransfer( transfer)
                {}

                //
                // Spelled as a call rather than as a convert()/apply() member,
                // so a sensor reads as the function it is at the point of use:
                // `dut::CoolantSensor( sense)`, with the sensor's name where a
                // reader looks for what turned volts into degrees.
                //
                [[nodiscard]]
                constexpr auto operator()( const SensedT sensed) const -> DerivedT
                {
                    return mTransfer( sensed);
                }

                //
                // What the log wants to say about a derived value: which sensor
                // produced it, and on what basis. Carried on the transducer
                // rather than restated wherever it is used, the same way
                // AdapterPointTag carries a point's Name/Description -- and for
                // the same reason, since "61.2 degC via CoolantSensor" is only
                // as good as the one place that spells out what CoolantSensor
                // is.
                //
                [[nodiscard]]
                constexpr auto name() const -> std::string_view
                {
                    return mName;
                }

                [[nodiscard]]
                constexpr auto description() const -> std::string_view
                {
                    return mDescription;
                }

            private:
                std::string_view mName;
                std::string_view mDescription;
                DerivedT       ( *mTransfer)( SensedT);
        };

        //
        // is_transducer / TransducerType: "this is some Transducer<S, D>",
        // without knowing which units. Mirrors is_quantity/QuantityType above
        // it in quantity.hpp, and exists for the same reason -- so generic code
        // can constrain on the shape rather than on a concrete pair. The caller
        // this is written for is anything that wants to accept a sensor and
        // then ask it what it derives (Transducer::Derived), a journal line
        // included.
        //
        template<typename T>
        struct is_transducer : std::false_type {};

        template<typename SensedT, typename DerivedT>
        struct is_transducer<Transducer<SensedT, DerivedT>> : std::true_type {};

        template<typename T>
        concept TransducerType = is_transducer<T>::value;
    } // namespace quantities
} // namespace core
