#include "hal/fabric/switch_fabric.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/meta.hpp"

//
// hal::SwitchFabric with a driver behind it: when a relay physically moves,
// against the use counts the fabric has always kept. A fake driver records
// every move, so the rule -- the first use closes, the last release opens,
// and nothing in between moves anything -- can be read off one list.
//
// Elements are made from whatever this deployment's first card is, with
// arbitrary channels: the fabric does not check a channel against its card
// (hal::hop does, at the call site), and nothing here reaches a card.
//
namespace
{
    class Recorded final : public hal::SwitchDriver
    {
        public:
            auto close( const hal::SwitchElementId id) -> void override
            {
                if( id.channel == RefuseClose)
                {
                    throw std::runtime_error( "refused");
                }

                Moves.push_back( "close " + std::to_string( id.channel));
            }

            auto open( const hal::SwitchElementId id) -> void override
            {
                if( id.channel == RefuseOpen)
                {
                    throw std::runtime_error( "refused");
                }

                Moves.push_back( "open " + std::to_string( id.channel));
            }

            std::vector<std::string> Moves;
            std::uint16_t            RefuseClose{ 0 };
            std::uint16_t            RefuseOpen{ 0 };
    };

    [[nodiscard]]
    auto element( const std::uint16_t channel) -> hal::SwitchElementId
    {
        return hal::SwitchElementId{ core::meta::values<hal::SwitchDeviceId>[ 0], channel };
    }

    using Moves = std::vector<std::string>;
} // namespace

class SwitchFabricDriver : public ::testing::Test
{
    protected:
        void SetUp() override
        {
            if( core::meta::values<hal::SwitchDeviceId>.empty())
            {
                GTEST_SKIP() << "this deployment declares no switching device to make an element of";
            }
        }

        Recorded          Driver;
        hal::SwitchFabric Fabric{ Driver };
};

TEST_F( SwitchFabricDriver, TheFirstUseClosesTheRelayAndTheLastReleaseOpensIt)
{
    Fabric.close( element( 101));
    Fabric.close( element( 101));
    Fabric.open( element( 101));

    EXPECT_EQ( Driver.Moves, ( Moves{ "close 101" })) << "a second use and a first release move nothing";
    EXPECT_TRUE( Fabric.isClosed( element( 101)));

    Fabric.open( element( 101));

    EXPECT_EQ( Driver.Moves, ( Moves{ "close 101", "open 101" }));
    EXPECT_FALSE( Fabric.isClosed( element( 101)));
}

TEST_F( SwitchFabricDriver, ReleasingWhatNobodyHoldsMovesNothing)
{
    Fabric.open( element( 101));

    EXPECT_TRUE( Driver.Moves.empty());
}

//
// A refused close is not counted: the element is open on the books as it is
// on the bench, and a later close tries again.
//
TEST_F( SwitchFabricDriver, ARefusedCloseIsNotCountedAsClosed)
{
    Driver.RefuseClose = 101;

    EXPECT_THROW( Fabric.close( element( 101)), std::runtime_error);
    EXPECT_FALSE( Fabric.isClosed( element( 101)));

    Driver.RefuseClose = 0;

    Fabric.close( element( 101));

    EXPECT_EQ( Driver.Moves, ( Moves{ "close 101" }));
}

//
// A path is all or nothing: the third element refused, the first two -- already
// closed by this call -- are released before the failure goes on.
//
TEST_F( SwitchFabricDriver, AConnectThatFailsPartWayReleasesWhatItClosed)
{
    Driver.RefuseClose = 103;

    EXPECT_THROW( Fabric.connect( { element( 101), element( 102), element( 103) }), std::runtime_error);

    EXPECT_EQ( Driver.Moves, ( Moves{ "close 101", "close 102", "open 102", "open 101" }));
    EXPECT_FALSE( Fabric.isClosed( element( 101)));
    EXPECT_FALSE( Fabric.isClosed( element( 102)));
}

//
// ...and releases only what *it* closed: an element some other route already
// held stays held, because the rollback is a release of this call's uses, not
// an open of the relay.
//
TEST_F( SwitchFabricDriver, ARollbackLeavesAnotherRoutesElementsHeld)
{
    Fabric.close( element( 101));

    Driver.RefuseClose = 103;

    EXPECT_THROW( Fabric.connect( { element( 101), element( 103) }), std::runtime_error);

    EXPECT_TRUE( Fabric.isClosed( element( 101)));
    EXPECT_EQ( Driver.Moves, ( Moves{ "close 101" }));
}

//
// A release is all of the path even when part of it fails: every element is
// released, and the first failure reported once they all have been.
//
TEST_F( SwitchFabricDriver, ADisconnectReleasesEveryElementEvenWhenOneFails)
{
    Fabric.connect( { element( 101), element( 102), element( 103) });

    Driver.RefuseOpen = 102;

    EXPECT_THROW( Fabric.disconnect( { element( 101), element( 102), element( 103) }), std::runtime_error);

    EXPECT_FALSE( Fabric.isClosed( element( 101)));
    EXPECT_FALSE( Fabric.isClosed( element( 102)));
    EXPECT_FALSE( Fabric.isClosed( element( 103)));
    EXPECT_EQ( Driver.Moves.back(), "open 103");
}

//
// openAll() forgets every use and moves nothing: safing's relay pass has opened
// the hardware already, and safing may not talk through a driver.
//
TEST_F( SwitchFabricDriver, OpenAllIsBookkeepingOnly)
{
    Fabric.connect( { element( 101), element( 102) });

    const auto before = Driver.Moves.size();

    Fabric.openAll();

    EXPECT_EQ( Driver.Moves.size(), before);
    EXPECT_FALSE( Fabric.isClosed( element( 101)));
}

//
// And with no driver, the fabric is what it always was.
//
TEST( SwitchFabricWithoutDriver, IsBookkeeping)
{
    if( core::meta::values<hal::SwitchDeviceId>.empty())
    {
        GTEST_SKIP() << "this deployment declares no switching device";
    }

    hal::SwitchFabric fabric;

    fabric.close( element( 101));

    EXPECT_TRUE( fabric.isClosed( element( 101)));
}
