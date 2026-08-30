#include "core/quantities/transducer.hpp"
#include "core/quantities/quantity_kind.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <type_traits>

using namespace core::literals;
using namespace core::quantities;

namespace
{
    //
    // Stands in for the sensor a DUT would declare beside its adapter table:
    // an LM35 fitted to a pin, 10 mV per degree, zero volts at zero degrees.
    // Declared constexpr on purpose -- that a Transducer can be a compile-time
    // value is why the transfer is a function pointer rather than a
    // std::function, and it is the property that lets a sensor sit next to a
    // POINT rather than be built at startup.
    //
    constexpr auto CoolantSensor = Transducer<Voltage, Temperature>{
        "CoolantSensor",
        "LM35 on TP4 -- 10 mV/degC, 0 V at 0 degC",
        +[]( const Voltage sensed) -> Temperature { return Temperature{ sensed.value() * 100.0 }; } };

    //
    // A second sensor deriving the same unit from the same unit, to make the
    // point that two of these are two distinguishable named values rather than
    // interchangeable lambdas -- and a curve with a bend in it, to make the
    // point that nothing here assumes the mapping is linear.
    //
    constexpr auto IntakeSensor = Transducer<Voltage, Temperature>{
        "IntakeSensor",
        "NTC divider on TP7 -- quadratic fit over 0..80 degC",
        +[]( const Voltage sensed) -> Temperature
        {
            const auto v = sensed.value();

            return Temperature{ 12.5 * v * v + 40.0 * v - 5.0 };
        } };

    template<typename SensorT, typename ArgT>
    concept AcceptsReading = requires( const SensorT & sensor, ArgT reading) { sensor( reading); };
}

TEST( CoreTransducer, DerivesTheQuantityTheSensorWasWrittenFor)
{
    const auto sensed = 0.612_V;

    const Temperature coolant = CoolantSensor( sensed);

    EXPECT_DOUBLE_EQ( coolant.value(), 61.2);
}

//
// The whole point of the type, in one line: what comes back is a Temperature,
// not a double that happens to hold one, so a criterion in degrees can check
// it and a criterion in volts cannot.
//
TEST( CoreTransducer, ResultIsTheDerivedQuantityType)
{
    static_assert( std::is_same_v<decltype( CoolantSensor( 0.612_V)), Temperature>);
    static_assert( std::is_same_v<Transducer<Voltage, Temperature>::Sensed,  Voltage>);
    static_assert( std::is_same_v<Transducer<Voltage, Temperature>::Derived, Temperature>);

    EXPECT_EQ( core::quantityKindOf<decltype( CoolantSensor( 0.612_V))>(), core::QuantityKind::Temperature);
}

TEST( CoreTransducer, ConvertsInAConstantExpression)
{
    constexpr auto coolant = CoolantSensor( Voltage{ 0.25});

    static_assert( coolant.value() == 25.0);

    EXPECT_DOUBLE_EQ( coolant.value(), 25.0);
}

//
// Nothing here assumes a straight line -- the transfer is opaque, so a bent
// curve is simply a longer body.
//
TEST( CoreTransducer, DerivesThroughANonLinearCurve)
{
    const Temperature intake = IntakeSensor( 0.8_V);

    EXPECT_DOUBLE_EQ( intake.value(), 12.5 * 0.64 + 32.0 - 5.0);
}

//
// Two sensors deriving the same unit stay two distinct, named things -- which
// is what a journal line and a reader of the script both need, and what two
// anonymous lambdas at two call sites would not give.
//
TEST( CoreTransducer, CarriesItsOwnNameAndDescription)
{
    EXPECT_EQ( CoolantSensor.name(), "CoolantSensor");
    EXPECT_EQ( IntakeSensor.name(),  "IntakeSensor");

    EXPECT_NE( CoolantSensor.description(), IntakeSensor.description());
    EXPECT_NE( CoolantSensor( 0.5_V), IntakeSensor( 0.5_V));
}

TEST( CoreTransducer, DeducesItsUnitsFromTheTransferFunction)
{
    constexpr auto shuntCurrent = Transducer{
        "ShuntSense",
        "10 mOhm shunt -- Ohm's law, in a sensor's clothing",
        +[]( const Voltage drop) -> Current { return drop / 10.0_mOhm; } };

    static_assert( std::is_same_v<decltype( shuntCurrent)::Derived, Current>);

    EXPECT_DOUBLE_EQ( shuntCurrent( 30.0_mV).value(), 3.0);
}

//
// The negative half, expressed as concepts rather than bare requires-
// expressions -- see the note at the top of criteria/test_static_constraints.cpp
// on why that distinction matters to both compilers.
//
// This is where the safety actually is: a sensor cannot be handed a reading in
// the wrong unit, and cannot be handed back what it just produced.
//
TEST( CoreTransducer, RefusesAReadingInTheWrongUnit)
{
    using CoolantT = decltype( CoolantSensor);

    static_assert(  AcceptsReading<CoolantT, Voltage> );
    static_assert( !AcceptsReading<CoolantT, Current> );
    static_assert( !AcceptsReading<CoolantT, Resistance> );
    static_assert( !AcceptsReading<CoolantT, Temperature> );
    static_assert( !AcceptsReading<CoolantT, double> );
}

//
// A sensor is free to derive a difference rather than a point -- a delta-T
// probe across a heatsink answers "how much hotter", which is a
// TemperatureDelta and not a Temperature. Worth pinning down, because the two
// are exactly the pair the unit system now keeps apart: a sensor that derived
// the wrong one of them would be caught here, and at every call site after.
//
TEST( CoreTransducer, MayDeriveADifferenceRatherThanAPoint)
{
    constexpr auto riseProbe = Transducer{
        "HeatsinkRise",
        "Differential thermopile across the heatsink -- 5 mV/K",
        +[]( const Voltage sensed) -> TemperatureDelta { return TemperatureDelta{ sensed.value() * 200.0 }; } };

    static_assert( std::is_same_v<decltype( riseProbe)::Derived, TemperatureDelta>);

    const auto rise = riseProbe( 0.2_V);

    EXPECT_DOUBLE_EQ( rise.value(), 40.0);

    //
    // And it composes with the affine arithmetic the way a reader would expect:
    // ambient plus a rise is a temperature again.
    //
    const Temperature hotspot = 21.5_degC + rise;

    EXPECT_DOUBLE_EQ( hotspot.value(), 61.5);
}

TEST( CoreTransducer, TransducerTypeRecognisesOnlyTransducers)
{
    static_assert(  TransducerType<Transducer<Voltage, Temperature>> );
    static_assert( !TransducerType<Voltage> );
    static_assert( !TransducerType<double> );
}
