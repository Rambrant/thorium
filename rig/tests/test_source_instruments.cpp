//
// What remains of a file that held hal::N6701A's and hal::Ac6834B's tests
// together: the one test that genuinely needs both. The rest went to the drivers
// when they moved out to instruments/ -- see instruments/n6701a/tests/ and
// instruments/ac6834b/tests/, each of which now carries its own trimmed copy of
// the fixture the two used to share.
//
// A test that names two instruments cannot live in either one's directory
// without making that directory depend on the other driver, which is exactly
// what would stop it being packageable on its own (see instruments/README.md).
// It is really a test of Connect/Disconnect being additive rather than of
// either supply, which is why it is here rather than in framework/hal/tests/ too:
// this is a rig-level test, and rig/tests/ is where those live now.
//
#include "hal/n6701a.hpp"
#include "hal/ac6834b.hpp"
#include "hal/verbs/route.hpp"

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

        hal::N6701ARelay       dcP3{ hal::InstrumentId::DcP3, hal::Simulated{}, 3 };
        hal::Ac6834B           acP1{ hal::InstrumentId::AcP1, hal::Simulated{} };

        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

        SourceInstrumentFixture()
        {
            instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceId::Spst1, 4 });

            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 0 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 1 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 2 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 3 });
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
    connect( acP1.ac());

    disconnect( dcP3.dc());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 4 }));
    EXPECT_TRUE(  fabric.isClosed( { hal::SwitchDeviceId::Spst1, 0 }));
}
