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

TEST( HalConnectorWiring, RfMuxHopsComposeThroughTheDeclarativeWiringMacrosLikeAnyOtherDeviceKind)
{
    // WIRE_CONNECTOR/HOP (see hal/wiring.hpp) never singled out Matrix/Mux
    // by name -- deviceKind is just whichever hal::SwitchDeviceKind value
    // is passed, so an RF path through this rig's connector wiring reads
    // exactly like an LF one, no separate macro needed.
    hal::ConnectorWiring wiring;
    hal::VpcLocation     location{ hal::VpcRack::A, 5, 1 };

    wiring.addWire( location, hal::Path{ HOP( RfMux, "RfMux1", 2), HOP( Matrix, "Matrix2", 30) });

    EXPECT_EQ( wiring.find( location), ( hal::Path{
        hal::SwitchElementId{ hal::SwitchDeviceKind::RfMux,  "RfMux1",  2 },
        hal::SwitchElementId{ hal::SwitchDeviceKind::Matrix, "Matrix2", 30 } }));
}

TEST( HalInstrumentWiring, FindOnlyEverReturnsForceRoleEntriesNotSense)
{
    hal::InstrumentWiring wiring;
    hal::SwitchElementId  force{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };
    hal::SwitchElementId  sense{ hal::SwitchDeviceKind::Matrix, "Matrix2", 15 };

    wiring.addWire( hal::InstrumentId::Dmm1, force);
    wiring.addWire( hal::InstrumentId::Dmm1, sense, hal::WireRole::Sense);

    EXPECT_EQ( wiring.find( hal::InstrumentId::Dmm1), (hal::Path{ force }));
}

TEST( HalInstrumentWiring, FindSenseReturnsOnlyTheSenseRoleEntry)
{
    hal::InstrumentWiring wiring;
    hal::SwitchElementId  force{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };
    hal::SwitchElementId  sense{ hal::SwitchDeviceKind::Matrix, "Matrix2", 15 };

    wiring.addWire( hal::InstrumentId::Dmm1, force);
    wiring.addWire( hal::InstrumentId::Dmm1, sense, hal::WireRole::Sense);

    EXPECT_EQ( wiring.findSense( hal::InstrumentId::Dmm1), (hal::Path{ sense }));
}

TEST( HalInstrumentWiring, FindSenseThrowsWhenNoSenseEntryIsRegistered)
{
    hal::InstrumentWiring wiring;
    wiring.addWire( hal::InstrumentId::Dmm1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 14 });

    // Dmm1 has a Force entry but no Sense one -- a 4-wire measurement
    // attempted on it should fail loudly, not silently fall back to force.
    EXPECT_THROW( (void)wiring.findSense( hal::InstrumentId::Dmm1), std::runtime_error);
}

TEST( HalInstrumentWiring, FindAllIgnoresRoleAndReturnsForceAndSenseTogether)
{
    // findAll() is the "close everything for this instrument together"
    // lookup -- see its own comment -- so unlike find()/findSense(), it
    // deliberately does not filter by role at all.
    hal::InstrumentWiring wiring;
    hal::SwitchElementId  force{ hal::SwitchDeviceKind::Matrix, "Matrix2", 20 };
    hal::SwitchElementId  sense{ hal::SwitchDeviceKind::Matrix, "Matrix2", 21 };

    wiring.addWire( hal::InstrumentId::DcP3, force);
    wiring.addWire( hal::InstrumentId::DcP3, sense, hal::WireRole::Sense);

    EXPECT_EQ( wiring.findAll( hal::InstrumentId::DcP3), (hal::Path{ force, sense }));
}

TEST( HalInstrumentWiring, WireInstrumentSenseMacroTagsTheEntryAsSense)
{
    using namespace hal; // the macros expand to unqualified InstrumentId/Path/WireRole, as they do inside INSTRUMENT_WIRING's own namespace hal {} block

    // WIRE_INSTRUMENT/WIRE_INSTRUMENT_SENSE push into an `entries` vector --
    // the same shape the connector-side macros use, and for the same reason
    // (see hal/wiring.hpp): those entries feed both the runtime
    // hal::instrumentWiring and the compile-time key table isInstrumentWired()
    // reads, so they can no longer write straight into an InstrumentWiring.
    std::vector<InstrumentWiringEntry> entries;

    WIRE_INSTRUMENT(       Dmm1, HOP( Matrix, "Matrix2", 14));
    WIRE_INSTRUMENT_SENSE( Dmm1, HOP( Matrix, "Matrix2", 15));

    ASSERT_EQ( entries.size(), 2u);
    EXPECT_EQ( entries[ 0].role, WireRole::Force);
    EXPECT_EQ( entries[ 1].role, WireRole::Sense);

    hal::InstrumentWiring w;
    for( const auto & entry : entries) w.addWire( entry.instrument, entry.path, entry.role);

    EXPECT_EQ( w.find( hal::InstrumentId::Dmm1),      (hal::Path{ { hal::SwitchDeviceKind::Matrix, "Matrix2", 14 } }));
    EXPECT_EQ( w.findSense( hal::InstrumentId::Dmm1), (hal::Path{ { hal::SwitchDeviceKind::Matrix, "Matrix2", 15 } }));
}

TEST( HalConnectorWiring, FindSenseReturnsOnlyTheSenseRoleEntry)
{
    hal::ConnectorWiring wiring;
    hal::VpcLocation     location{ hal::VpcRack::A, 1, 3 };
    hal::SwitchElementId force{ hal::SwitchDeviceKind::Mux, "Mux1", 3 };
    hal::SwitchElementId sense{ hal::SwitchDeviceKind::Mux, "Mux1", 4 };

    wiring.addWire( location, force);
    wiring.addWire( location, sense, hal::WireRole::Sense);

    EXPECT_EQ( wiring.find( location),      (hal::Path{ force }));
    EXPECT_EQ( wiring.findSense( location), (hal::Path{ sense }));
}

TEST( HalConnectorWiring, FindSenseThrowsWhenNoSenseEntryIsRegistered)
{
    hal::ConnectorWiring wiring;
    hal::VpcLocation     location{ hal::VpcRack::A, 1, 3 };
    wiring.addWire( location, { hal::SwitchDeviceKind::Mux, "Mux1", 3 });

    EXPECT_THROW( (void)wiring.findSense( location), std::runtime_error);
}

TEST( HalConnectorWiring, WireConnectorSenseMacroTagsTheEntryAsSense)
{
    using namespace hal; // WIRE_CONNECTOR_SENSE expands to unqualified VpcLocation/VpcRack/Path/WireRole/ConnectorWiringEntry, as it does inside CONNECTOR_WIRING's own namespace hal {} block

    // WIRE_CONNECTOR_SENSE builds a ConnectorWiringEntry (see CONNECTOR_WIRING's
    // own comment in hal/wiring.hpp on why -- it also feeds a compile-time
    // key table END_CONNECTOR_WIRING derives from these entries, which a
    // ConnectorWiring instance's own runtime storage can't be promoted to),
    // not a ConnectorWiring directly -- addWire() below is the one-line
    // bridge back to a queryable instance, same as END_CONNECTOR_WIRING's.
    std::vector<ConnectorWiringEntry> entries;
    WIRE_CONNECTOR_SENSE( A, 1, 3, HOP( Mux, "Mux1", 4));

    hal::ConnectorWiring w;
    for( const auto & entry : entries) w.addWire( entry.location, entry.path, entry.role);

    EXPECT_EQ( w.findSense( ( hal::VpcLocation{ hal::VpcRack::A, 1, 3 })),
               (hal::Path{ { hal::SwitchDeviceKind::Mux, "Mux1", 4 } }));
}

//
// ---------------------------------------------------------------------------
// SourceWiring -- the pins nothing routes to
// ---------------------------------------------------------------------------
// See hal::SourceWiring's own comment in hal/wiring.hpp. Note there is no Path
// anywhere below: the whole content of an entry is that the fabric is not
// involved, which is why this table has its own type rather than being a
// ConnectorWiring entry with an empty path.
//
TEST( HalSourceWiring, FindReturnsTheInstrumentCabledOntoAPin)
{
    hal::SourceWiring wiring;
    hal::VpcLocation  backupSupply{ hal::VpcRack::A, 1, 5 };

    wiring.addLanding( hal::InstrumentId::DcP2, backupSupply);

    EXPECT_EQ( wiring.find( backupSupply), hal::InstrumentId::DcP2);
}

TEST( HalSourceWiring, FindThrowsForAPinNoSourceLandsOn)
{
    hal::SourceWiring wiring;

    EXPECT_THROW( (void)wiring.find( ( hal::VpcLocation{ hal::VpcRack::A, 1, 3 })), std::runtime_error);
}

TEST( HalSourceWiring, FindAllReturnsEveryPinAMultiPinSourceLandsOn)
{
    // AcP1's shape: three phases plus the neutral/ground return, four
    // independent landings under one InstrumentId -- the connector-side twin
    // of the four WIRE_INSTRUMENT rows it already has (see rig/wiring.inc).
    hal::SourceWiring wiring;
    hal::VpcLocation  phaseA{ hal::VpcRack::A, 3, 1 };
    hal::VpcLocation  phaseB{ hal::VpcRack::A, 3, 3 };
    hal::VpcLocation  phaseC{ hal::VpcRack::A, 3, 5 };
    hal::VpcLocation  neutral{ hal::VpcRack::A, 3, 7 };

    wiring.addLanding( hal::InstrumentId::AcP1, phaseA);
    wiring.addLanding( hal::InstrumentId::AcP1, phaseB);
    wiring.addLanding( hal::InstrumentId::AcP1, phaseC);
    wiring.addLanding( hal::InstrumentId::AcP1, neutral);

    EXPECT_EQ( wiring.findAll( hal::InstrumentId::AcP1),
               (std::vector<hal::VpcLocation>{ phaseA, phaseB, phaseC, neutral }));
}

TEST( HalSourceWiring, FindAllIsEmptyRatherThanThrowingForARoutedInstrument)
{
    // Unlike InstrumentWiring::findAll(), which throws for an instrument with
    // no fixed path -- see this method's own comment in hal/wiring.hpp. Every
    // routed instrument on a rig lands nowhere, so "nowhere" is the ordinary
    // answer here, not an error.
    hal::SourceWiring wiring;
    wiring.addLanding( hal::InstrumentId::DcP2, ( hal::VpcLocation{ hal::VpcRack::A, 1, 5 }));

    EXPECT_TRUE( wiring.findAll( hal::InstrumentId::Dmm1).empty());
}

TEST( HalSourceWiring, WireSourceMacroBuildsALandingEntry)
{
    using namespace hal; // WIRE_SOURCE expands unqualified, as it does inside SOURCE_WIRING's own namespace hal {} block

    // Same shape as WireConnectorSenseMacroTagsTheEntryAsSense above, and for
    // the same reason: WIRE_SOURCE builds a SourceWiringEntry (which
    // END_SOURCE_WIRING also promotes to a compile-time array), not a
    // SourceWiring directly -- addLanding() is the one-line bridge back.
    std::vector<SourceWiringEntry> entries;
    WIRE_SOURCE( DcP1, A, 1, 3);

    ASSERT_EQ( entries.size(), 1u);

    hal::SourceWiring w;
    for( const auto & entry : entries) w.addLanding( entry.instrument, entry.location);

    EXPECT_EQ( w.find( ( hal::VpcLocation{ hal::VpcRack::A, 1, 3 })), hal::InstrumentId::DcP1);
}
