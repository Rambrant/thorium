#include "hal/wiring.hpp"

#include <gtest/gtest.h>

TEST( HalInstrumentWiring, FindReturnsASingleHopPathForTheCommonCase)
{
    hal::InstrumentWiring wiring;
    hal::SwitchElementId  channel{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };

    wiring.addWire( hal::InstrumentId::Dmm1, channel);

    EXPECT_EQ( wiring.find( hal::InstrumentId::Dmm1), (hal::Path{ channel }));
}

TEST( HalInstrumentWiring, FindThrowsWhenAnInstrumentIsNotWired)
{
    hal::InstrumentWiring wiring;

    EXPECT_THROW( (void)wiring.find( hal::InstrumentId::Dmm1), std::runtime_error);
}

TEST( HalInstrumentWiring, AddWireWithAPathModelsAMultiHopChain)
{
    // A single fixed instrument connection that itself passes through more
    // than one relay before its dedicated matrix column -- see
    // hal::InstrumentWiring's own comment. One addWire() call, one Path
    // with several elements, not several addWire() calls (that's
    // findAll()'s shape, for independent connections -- see below).
    hal::InstrumentWiring wiring;
    hal::SwitchElementId  isolationRelay{ hal::SwitchDeviceKind::Matrix, "ACPBox", 1 };
    hal::SwitchElementId  matrixColumn{ hal::SwitchDeviceKind::Matrix, "Matrix2", 22 };

    wiring.addWire( hal::InstrumentId::AcP1, hal::Path{ isolationRelay, matrixColumn });

    EXPECT_EQ( wiring.find( hal::InstrumentId::AcP1), (hal::Path{ isolationRelay, matrixColumn }));
}

TEST( HalInstrumentWiring, FindAllFlattensEveryIndependentPathIntoOne)
{
    hal::InstrumentWiring wiring;
    hal::SwitchElementId  phaseA{ hal::SwitchDeviceKind::Matrix, "Matrix2", 22 };
    hal::SwitchElementId  phaseB{ hal::SwitchDeviceKind::Matrix, "Matrix2", 23 };

    wiring.addWire( hal::InstrumentId::AcP1, phaseA);
    wiring.addWire( hal::InstrumentId::AcP1, phaseB);

    EXPECT_EQ( wiring.findAll( hal::InstrumentId::AcP1), (hal::Path{ phaseA, phaseB }));
}

TEST( HalInstrumentWiring, FindAllFlattensMultiHopEntriesTooNotJustSingleHopOnes)
{
    // findAll() combines *every* matching entry's Path -- including entries
    // that are themselves multi-hop chains, not just single elements.
    hal::InstrumentWiring wiring;
    hal::SwitchElementId  isolationRelay{ hal::SwitchDeviceKind::Matrix, "ACPBox", 1 };
    hal::SwitchElementId  phaseA{ hal::SwitchDeviceKind::Matrix, "Matrix2", 22 };
    hal::SwitchElementId  ground{ hal::SwitchDeviceKind::Matrix, "Matrix2", 27 };

    wiring.addWire( hal::InstrumentId::AcP1, hal::Path{ isolationRelay, phaseA });
    wiring.addWire( hal::InstrumentId::AcP1, ground);

    EXPECT_EQ( wiring.findAll( hal::InstrumentId::AcP1), (hal::Path{ isolationRelay, phaseA, ground }));
}

TEST( HalInstrumentWiring, FindAllThrowsWhenAnInstrumentIsNotWired)
{
    hal::InstrumentWiring wiring;

    EXPECT_THROW( (void)wiring.findAll( hal::InstrumentId::Dmm1), std::runtime_error);
}

TEST( HalConnectorWiring, FindReturnsASingleHopPathForTheCommonCase)
{
    hal::ConnectorWiring wiring;
    hal::VpcLocation     location{ hal::VpcRack::A, 1, 3 };
    hal::SwitchElementId channel{ hal::SwitchDeviceKind::Mux, "Mux1", 3 };

    wiring.addWire( location, channel);

    EXPECT_EQ( wiring.find( location), (hal::Path{ channel }));
}

TEST( HalConnectorWiring, FindThrowsWhenAPinIsNotWired)
{
    hal::ConnectorWiring wiring;

    EXPECT_THROW( (void)wiring.find( hal::VpcLocation{ hal::VpcRack::A, 1, 3 }), std::runtime_error);
}

TEST( HalConnectorWiring, AddWireWithAPathModelsAMultiMuxChain)
{
    // The INI-file shape that motivated this: a connector reached through
    // two muxes narrowing down before a matrix row, all one fixed Path.
    hal::ConnectorWiring wiring;
    hal::VpcLocation     location{ hal::VpcRack::A, 3, 1 };
    hal::SwitchElementId mux1{ hal::SwitchDeviceKind::Mux, "Mux31", 9 };
    hal::SwitchElementId mux2{ hal::SwitchDeviceKind::Mux, "Mux22", 65 };
    hal::SwitchElementId matrixRow{ hal::SwitchDeviceKind::Matrix, "Matrix11", 0 };

    wiring.addWire( location, hal::Path{ mux1, mux2, matrixRow });

    EXPECT_EQ( wiring.find( location), (hal::Path{ mux1, mux2, matrixRow }));
}
