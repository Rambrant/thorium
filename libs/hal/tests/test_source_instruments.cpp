//
// What remains of a file that held hal::N6701A's and hal::Ac6677A's tests
// together: the one test that genuinely needs both. The rest went to the drivers
// when they moved out to instruments/ -- see instruments/n6701a/tests/ and
// instruments/ac6677a/tests/, each of which now carries its own trimmed copy of
// the fixture the two used to share.
//
// A test that names two instruments cannot live in either one's directory
// without making that directory depend on the other driver, which is exactly
// what would stop it being packageable on its own (see instruments/README.md).
// So it stays at this level. It is really a test of Connect/Disconnect being
// additive rather than of either supply, and its natural home is a rig/tests
// target alongside test_call_syntax.cpp and test_safing.cpp -- still to be
// created.
//
#include "hal/n6701a.hpp"
#include "hal/ac6677a.hpp"
#include "hal/apply.hpp"

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

namespace
{
    struct SourceInstrumentFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        hal::N6701ARelay       dcP3{ hal::InstrumentId::DcP3, 3 };
        hal::Ac6677A           acP1{ hal::InstrumentId::AcP1 };

        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

        SourceInstrumentFixture()
        {
            instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 });

            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 23 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 26 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 27 });
        }
    };
} // namespace

TEST_F( SourceInstrumentFixture, DcConnectAndDisconnectDoNotDisturbAnUnrelatedAlreadyConnectedPath)
{
    // acP1's phase A and dcP3's relay are two independent fixed channels.
    // Connecting/disconnecting one must leave the other's relay alone --
    // the whole reason connect()/disconnect() are additive rather than
    // route()'s "make this the one live path" behaviour.
    connect( dcP3.dc());
    connect( acP1.threePhaseWye());

    disconnect( dcP3.dc());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 }));
    EXPECT_TRUE(  fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 }));
}
