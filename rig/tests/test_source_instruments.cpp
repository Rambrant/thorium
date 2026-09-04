//
// What remains of a file that held hal::keysight_edu36311a::EDU36311A's and hal::keysight_ac6834b::Ac6834B's tests
// together: the one test that genuinely needs both. The rest went to the drivers
// when they moved out to instruments/ -- see instruments/keysight_edu36311a/tests/ and
// instruments/keysight_ac6834b/tests/, each of which now carries its own trimmed copy of
// the fixture the two used to share.
//
// A test that names two instruments cannot live in either one's directory
// without making that directory depend on the other driver, which is exactly
// what would stop it being packageable on its own (see instruments/README.md).
// It is really a test of Connect/Disconnect being additive rather than of
// either supply, which is why it is here rather than in framework/hal/tests/ too:
// this is a rig-level test, and rig/tests/ is where those live now.
//
#include "hal/keysight_edu36311a.hpp"
#include "hal/keysight_ac6834b.hpp"
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

        hal::keysight_edu36311a::RelayOutput3       dcP7{ hal::InstrumentId::DcP7, hal::Simulated{} };
        hal::keysight_ac6834b::Ac6834B           acP1{ hal::InstrumentId::AcP1, hal::Simulated{} };

        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

        SourceInstrumentFixture()
        {
            instrumentWiring.addWire( hal::InstrumentId::DcP7, { hal::SwitchDeviceId::Spst1, 7 });

            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 0 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 1 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 2 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 3 });
        }
    };
} // namespace

TEST_F( SourceInstrumentFixture, DcConnectAndDisconnectDoNotDisturbAnUnrelatedAlreadyConnectedPath)
{
    // acP1's phase A and dcP7's relay are two independent fixed channels.
    // Connecting/disconnecting one must leave the other's relay alone --
    // the whole reason connect()/disconnect() are additive rather than
    // route()'s "make this the one live path" behaviour.
    connect( dcP7.dc());
    connect( acP1.ac());

    disconnect( dcP7.dc());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 7 }));
    EXPECT_TRUE(  fabric.isClosed( { hal::SwitchDeviceId::Spst1, 0 }));
}
