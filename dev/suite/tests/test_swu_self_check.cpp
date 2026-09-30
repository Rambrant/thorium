#include "dev/suite/scripts.hpp"

//
// The SwitchUnit scripts, against a simulated chassis.
//
// Not detached, unlike the other instruments' script tests, because detaching
// would not help: these scripts call the chassis driver directly
// (Swu1.close(), not Connect()), and only the verbs consult the bench. So the
// fixture points Swu1 at a hal::Simulated address instead -- the driver's own
// simulation keeps a closed-channel set, answers ROUT:CLOS? from it and counts
// relay cycles -- and puts the table's address back afterwards.
//
// The one script the simulation cannot answer is the refusal: a simulated
// chassis knows the slots and not the modules, so it has no idea (@1050) is
// not a 34921A channel. That test hands Swu1 a fake transport instead, which
// answers the close with the error the mainframe would queue.
//
#include "suite/tests/verdict.hpp"

#include "hal/io/transport.hpp"
#include "hal/keysight_34980a.hpp"
#include "hal/topology/active_instruments.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using hal::keysight_34980a::ChannelAddress;
using hal::keysight_34980a::ModuleIdentity;

namespace
{
    struct SwitchUnitFixture : ::testing::Test
    {
        protected:

            void SetUp() override
            {
                mTableAddress = Swu1.address();

                Swu1.useAddress( hal::Simulated{});

                for( int slot = 1; slot <= 4; ++slot)
                {
                    Swu1.setSimulatedModule( slot, ModuleIdentity{ "Agilent Technologies", "34921A", "MY0000000" + std::to_string( slot), "2.0" });
                }

                Swu1.setSimulatedModule( 5, ModuleIdentity{ "Agilent Technologies", "34941A", "MY00000005", "2.0" });
            }

            void TearDown() override
            {
                Swu1.safeRelays();
                Swu1.useAddress( mTableAddress);
            }

        private:

            hal::Address mTableAddress;
    };

    //
    // A mainframe that answers the identity check and refuses one command --
    // queuing the -221 a 34980A answers a channel its module does not have
    // with, the way a SCPI instrument reports anything: on the next SYST:ERR?.
    //
    class RefusingChassis final : public hal::io::ITransport
    {
        public:
            explicit RefusingChassis( std::string refused) : mRefused( std::move( refused)) {}

            auto send( const std::string_view command) -> void override
            {
                if( command == "*IDN?")
                {
                    mReplies.emplace_back( "Agilent Technologies,34980A,MY44000001,2.43-2.42-1.19");
                }
                else if( command == "SYST:ERR?")
                {
                    mReplies.emplace_back( mPending.empty() ? "+0,\"No error\"" : mPending);
                    mPending.clear();
                }
                else if( command == mRefused)
                {
                    mPending = "-221,\"Settings conflict; channel not valid for module\"";
                }
                else if( !command.empty() && command.back() == '?')
                {
                    mReplies.emplace_back( "0");
                }
            }

            auto receive() -> std::string override
            {
                if( mReplies.empty())
                {
                    throw hal::io::TransportTimeout( "nothing queued on the refusing chassis");
                }

                auto reply = mReplies.front();

                mReplies.erase( mReplies.begin());

                return reply;
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "refusing fake 34980A";
            }

        private:
            std::string              mRefused;
            std::string              mPending;
            std::vector<std::string> mReplies;
    };
} // namespace

//
// -- Inventory -------------------------------------------------------------------
//

TEST_F( SwitchUnitFixture, TheInventoryPassesWithFour34921AsAndAFifthModule)
{
    EXPECT_TRUE( verdictOf( swuInventory));
}

TEST_F( SwitchUnitFixture, TheInventoryFailsWhenASlotHoldsAnotherModule)
{
    Swu1.setSimulatedModule( 3, ModuleIdentity{ "Agilent Technologies", "34932A", "MY00000003", "2.0" });

    EXPECT_FALSE( verdictOf( swuInventory));
}

TEST_F( SwitchUnitFixture, TheInventoryFailsWhenSlotFiveIsEmpty)
{
    Swu1.setSimulatedModule( 5, ModuleIdentity{ "Agilent Technologies", "0", "0", "0", true });

    EXPECT_FALSE( verdictOf( swuInventory));
}

//
// -- Relays --------------------------------------------------------------------
//

TEST_F( SwitchUnitFixture, EverySlotsRelayCyclePasses)
{
    EXPECT_TRUE( verdictOf( swuRelaysSlot1));
    EXPECT_TRUE( verdictOf( swuRelaysSlot2));
    EXPECT_TRUE( verdictOf( swuRelaysSlot3));
    EXPECT_TRUE( verdictOf( swuRelaysSlot4));
}

//
// Each relay script leaves its slot as it found it -- all open -- so the next
// test in the run starts from a known rack.
//
TEST_F( SwitchUnitFixture, ARelayScriptLeavesItsSlotAllOpen)
{
    static_cast<void>( verdictOf( swuRelaysSlot2));

    EXPECT_TRUE( Swu1.simulatedClosedChannels().empty());
}

//
// And it starts from all open, whatever the slot was left in: a relay closed
// before the script runs does not make "its neighbour is open" pass or fail by
// accident.
//
TEST_F( SwitchUnitFixture, ARelayScriptStartsFromAllOpenWhateverItFinds)
{
    Swu1.close( ChannelAddress{ 1, 2 });

    EXPECT_TRUE( verdictOf( swuRelaysSlot1));
}

//
// -- Refusal and relay life --------------------------------------------------------
//

TEST_F( SwitchUnitFixture, AChannelTheMainframeRefusesPasses)
{
    Swu1.useTransport( std::make_unique<RefusingChassis>( "ROUT:CLOS (@1050)"));

    EXPECT_TRUE( verdictOf( swuRefusesAMissingChannel));

    Swu1.closeSession();
}

//
// The failure that test exists for: a mainframe -- or a driver -- that lets a
// bad channel through without an error is a relay that did not move reported
// as one that did. Here the simulation stands in for it, since it accepts any
// well-formed channel.
//
TEST_F( SwitchUnitFixture, AChannelThatIsSilentlyAcceptedFails)
{
    EXPECT_FALSE( verdictOf( swuRefusesAMissingChannel));
}

TEST_F( SwitchUnitFixture, ADrivenRelayMovesItsLifeCount)
{
    EXPECT_TRUE( verdictOf( swuRelayCycles));
}
