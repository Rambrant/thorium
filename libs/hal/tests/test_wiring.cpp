#include "hal/wiring.hpp"

#include <gtest/gtest.h>

TEST( HalInstrumentWiring, FindReturnsTheWiredChannel)
{
    hal::InstrumentWiring wiring;
    hal::SwitchElementId  channel{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };

    wiring.addWire( hal::InstrumentId::Dmm1, channel);

    EXPECT_EQ( wiring.find( hal::InstrumentId::Dmm1), channel);
}

TEST( HalInstrumentWiring, FindThrowsWhenAnInstrumentIsNotWired)
{
    hal::InstrumentWiring wiring;

    EXPECT_THROW( (void)wiring.find( hal::InstrumentId::Dmm1), std::runtime_error);
}

TEST( HalConnectorWiring, FindReturnsTheWiredChannel)
{
    hal::ConnectorWiring wiring;
    hal::VpcLocation     location{ hal::VpcRack::A, 1, 3 };
    hal::SwitchElementId channel{ hal::SwitchDeviceKind::Mux, "Mux1", 3 };

    wiring.addWire( location, channel);

    EXPECT_EQ( wiring.find( location), channel);
}

TEST( HalConnectorWiring, FindThrowsWhenAPinIsNotWired)
{
    hal::ConnectorWiring wiring;

    EXPECT_THROW( (void)wiring.find( hal::VpcLocation{ hal::VpcRack::A, 1, 3 }), std::runtime_error);
}
