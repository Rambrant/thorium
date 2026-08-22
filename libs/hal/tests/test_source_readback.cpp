#include "hal/ac6834b.hpp"
#include "hal/measure.hpp"
#include "hal/n6701a.hpp"
#include "hal/route.hpp"
#include "hal/source.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/journal.hpp"

//
// A source measuring its own output, over its own interface -- the reading that
// needs no routing (see core::MeasureEngine's point-free operator()).
//
// This is how rail current is read on a rig whose switching matrix carries
// signals only: 115 V at 5 A never reaches the matrix, so there is no routed
// measurement of it to make. The tests below pin down the two properties that
// makes this worth having as its own overload rather than a convenience --
// that it reads the instrument, and that it leaves the fabric completely alone.
//
using namespace core::literals;
using namespace core::quantities;

namespace
{
    class CapturingSink : public core::IJournalSink
    {
        public:
            auto onEvent( const core::JournalEvent & event) -> void override { Events.push_back( event); }

            std::vector<core::JournalEvent> Events;
    };

    struct SourceReadbackFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        hal::N6701ARelay  dcP3{ hal::InstrumentId::DcP3, hal::Simulated{}, 3 };
        hal::Ac6834B      acP1{ hal::InstrumentId::AcP1, hal::Simulated{} };

        ApplyEngine    apply{};
        ConnectEngine  connect{ fabric, instrumentWiring, connectorWiring };

        core::MeasureEngine<hal::SwitchFabric, hal::InstrumentWiring, hal::ConnectorWiring>
            measure{ fabric, instrumentWiring, connectorWiring };

        CapturingSink sink;

        SourceReadbackFixture()
        {
            instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceId::Matrix2, 24 });

            core::journal().clearSinks();
            core::journal().add( sink);
            core::journal().begin( core::RunInfo{});
        }

        ~SourceReadbackFixture() override { core::journal().clearSinks(); }
    };
} // namespace

TEST_F( SourceReadbackFixture, ASupplyReportsWhatItIsDelivering)
{
    apply( dcP3.dc().voltage( 24.0_V).currentLimit( 7.0_A));
    dcP3.setSimulatedOutputCurrent( 2.1_A);

    EXPECT_DOUBLE_EQ( measure( dcP3.measuredVoltage()).value(), 24.0);
    EXPECT_DOUBLE_EQ( measure( dcP3.measuredCurrent()).value(),  2.1);
}

//
// The reading is of the output, not of the setpoint: a disabled supply reads
// zero rather than reporting what it was last told to hold. That is what a
// script checking "is this rail really off" depends on.
//
TEST_F( SourceReadbackFixture, ADisabledSupplyReadsZeroRatherThanItsSetpoint)
{
    apply( dcP3.dc().voltage( 24.0_V));
    dcP3.setSimulatedOutputCurrent( 2.1_A);
    dcP3.removeOutput();

    EXPECT_DOUBLE_EQ( measure( dcP3.measuredVoltage()).value(), 0.0);
    EXPECT_DOUBLE_EQ( measure( dcP3.measuredCurrent()).value(), 0.0);
}

//
// The property that makes this a separate overload rather than sugar: there is
// no path to compose and no relay to close, so nothing this reading does can
// collide with whatever else is currently routed.
//
TEST_F( SourceReadbackFixture, AReadbackNeverTouchesTheFabric)
{
    apply(   dcP3.dc().voltage( 24.0_V));
    connect( dcP3.dc());

    const auto channel = hal::SwitchElementId{ hal::SwitchDeviceId::Matrix2, 24 };

    ASSERT_TRUE( fabric.isClosed( channel));

    // A routed measurement would close and reopen its own path; this one has
    // none. The supply's relay stays exactly as Connect left it.
    (void) measure( dcP3.measuredCurrent());

    EXPECT_TRUE( fabric.isClosed( channel));
}

TEST_F( SourceReadbackFixture, AReadbackWithNoWiringEntryAtAllStillWorks)
{
    // AcP1 has no InstrumentWiring entry in this fixture. A routed measurement
    // would throw looking one up; a readback never asks.
    acP1.applyOutput( 115.0_V, 50.0_Hz, 5.0_A);
    acP1.setSimulatedOutputCurrent( 4.8_A);

    EXPECT_DOUBLE_EQ( measure( acP1.measuredVoltage( hal::Phase::A)).value(), 115.0);
    EXPECT_DOUBLE_EQ( measure( acP1.measuredCurrent( hal::Phase::A)).value(),   4.8);
}

//
// Logged like any other Measure, but keyed by instrument and quantity rather
// than by a point name -- there is no point. That key is also what a script
// unit test injects against.
//
TEST_F( SourceReadbackFixture, AReadbackIsLoggedAndKeyedByInstrumentAndQuantity)
{
    dcP3.applyOutput( 24.0_V, std::nullopt);
    dcP3.setSimulatedOutputCurrent( 2.1_A);

    (void) measure( dcP3.measuredCurrent());

    ASSERT_EQ( sink.Events.size(), 1u);

    const auto & event = sink.Events[ 0];

    EXPECT_EQ( event.Method,     core::Verb::Measure);
    EXPECT_EQ( event.Subject,    "DcP3.Current");
    EXPECT_EQ( event.Instrument, "DcP3");
    EXPECT_EQ( event.Value,      "2.1 A");
    EXPECT_EQ( event.Unit,       "A");
}

//
// One instrument reports several quantities, so the session key has to
// distinguish them -- keying on the instrument alone would make a supply's
// voltage and its current the same injected slot.
//
TEST_F( SourceReadbackFixture, VoltageAndCurrentAreSeparatelyInjectable)
{
    measure.inject( "DcP3.Voltage", core::QuantityVariant{ 23.9_V });
    measure.inject( "DcP3.Current", core::QuantityVariant{  6.4_A });

    EXPECT_DOUBLE_EQ( measure( dcP3.measuredVoltage()).value(), 23.9);
    EXPECT_DOUBLE_EQ( measure( dcP3.measuredCurrent()).value(),  6.4);

    measure.useLive();
}
