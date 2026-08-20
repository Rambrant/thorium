#include "hal/switch_fabric.hpp"

#include <gtest/gtest.h>

#include "core/meta.hpp"

TEST( HalSwitchFabric, CloseThenIsClosedReflectsState)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId ch{ hal::SwitchDeviceId::Matrix2, 14 };

    EXPECT_FALSE( fabric.isClosed( ch));

    fabric.close( ch);
    EXPECT_TRUE( fabric.isClosed( ch));

    fabric.open( ch);
    EXPECT_FALSE( fabric.isClosed( ch));
}

TEST( HalSwitchFabric, OpenAllClearsEveryElement)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId a{ hal::SwitchDeviceId::Matrix2, 14 };
    hal::SwitchElementId b{ hal::SwitchDeviceId::Mux1,    3 };

    fabric.close( a);
    fabric.close( b);
    fabric.openAll();

    EXPECT_FALSE( fabric.isClosed( a));
    EXPECT_FALSE( fabric.isClosed( b));
}

TEST( HalSwitchFabric, ConnectTwiceThenDisconnectOnceLeavesTheSharedElementClosed)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId shared{ hal::SwitchDeviceId::Mux1, 6 };
    hal::SwitchElementId supplyOnly{ hal::SwitchDeviceId::Matrix2, 22 };
    hal::SwitchElementId dmmOnly{ hal::SwitchDeviceId::Matrix2, 14 };

    // A supply parked on `shared` (e.g. Connect(Ac1...) holding 115V live)...
    fabric.connect( { supplyOnly, shared });

    // ...then a DMM briefly listening in on that very same point.
    fabric.connect( { dmmOnly, shared });

    // The DMM is done -- but `shared` has two uses on it now, so it must
    // stay closed for the still-active supply.
    fabric.disconnect( { dmmOnly, shared });

    EXPECT_FALSE( fabric.isClosed( dmmOnly));
    EXPECT_TRUE( fabric.isClosed( shared));
    EXPECT_TRUE( fabric.isClosed( supplyOnly));
}

TEST( HalSwitchFabric, DisconnectingBothUsersFinallyOpensTheSharedElement)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId shared{ hal::SwitchDeviceId::Mux1, 6 };
    hal::SwitchElementId supplyOnly{ hal::SwitchDeviceId::Matrix2, 22 };
    hal::SwitchElementId dmmOnly{ hal::SwitchDeviceId::Matrix2, 14 };

    fabric.connect( { supplyOnly, shared });
    fabric.connect( { dmmOnly, shared });

    fabric.disconnect( { dmmOnly, shared });
    fabric.disconnect( { supplyOnly, shared });

    EXPECT_FALSE( fabric.isClosed( supplyOnly));
    EXPECT_FALSE( fabric.isClosed( dmmOnly));
    EXPECT_FALSE( fabric.isClosed( shared));
}

TEST( HalSwitchFabric, OpenWithoutAMatchingCloseIsHarmless)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId ch{ hal::SwitchDeviceId::Matrix2, 14 };

    fabric.open( ch); // never closed -- must not underflow or throw
    EXPECT_FALSE( fabric.isClosed( ch));
}

TEST( HalSwitchFabric, ToStringDescribesTheElement)
{
    hal::SwitchElementId ch{ hal::SwitchDeviceId::Matrix2, 14 };

    EXPECT_EQ( to_string( ch), "Matrix Matrix2 channel 14");
}

//
// A mux channel is an ordinary switch element, exactly as a matrix crosspoint
// is: nothing in SwitchFabric treats the kinds differently -- close/open/
// isClosed work identically whatever kind of card the channel is on.
//
// This used to be written with an RfMux element, to make the point about the
// kind SwitchFabric is least likely to have been written with in mind. It
// cannot be any more, and that is the change working rather than coverage
// quietly lost: an element now names a device the rig declared, this rig
// declares no RF selector (see rig/devices.inc), so no hal::SwitchElementId
// on this rig can be on one. The kind-level claims that used to ride on a
// phantom device are made directly against hal::SwitchDeviceKind below.
//
TEST( HalSwitchFabric, AMuxChannelIsAnOrdinarySwitchElementLikeAMatrixCrosspoint)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId mux{ hal::SwitchDeviceId::Mux1, 2 };

    EXPECT_FALSE( fabric.isClosed( mux));

    fabric.close( mux);
    EXPECT_TRUE( fabric.isClosed( mux));

    fabric.open( mux);
    EXPECT_FALSE( fabric.isClosed( mux));
}

TEST( HalSwitchFabric, ToStringTakesTheKindFromTheDeviceNotFromTheElement)
{
    // Both halves of the rendering come from the device now: the kind from
    // hal::kindOf (rig/devices.inc states it once per card) and the name by
    // reflection over SwitchDeviceId. Two channels on cards of different
    // kinds is what shows that, and it is also the property that used to be
    // spelled per hop and could therefore disagree with itself.
    hal::SwitchElementId matrix{ hal::SwitchDeviceId::Matrix2, 14 };
    hal::SwitchElementId mux{ hal::SwitchDeviceId::Mux1, 2 };

    EXPECT_EQ( to_string( matrix), "Matrix Matrix2 channel 14");
    EXPECT_EQ( to_string( mux),    "Mux Mux1 channel 2");

    EXPECT_EQ( hal::kindOf( hal::SwitchDeviceId::Matrix2), hal::SwitchDeviceKind::Matrix);
    EXPECT_EQ( hal::kindOf( hal::SwitchDeviceId::Mux1),    hal::SwitchDeviceKind::Mux);
}

TEST( HalSwitchFabric, EveryDeviceKindRendersAsItsOwnName)
{
    // The bug this guards against: a kind silently printing as "Mux" because
    // to_string() only ever checked for Matrix and treated everything else as
    // Mux -- true right up until RfMux became a third kind. Made against the
    // enum rather than through an element, which is the only way to state it
    // for a kind no rig-declared device has; see the comment above.
    EXPECT_EQ( core::meta::to_string( hal::SwitchDeviceKind::Matrix), "Matrix");
    EXPECT_EQ( core::meta::to_string( hal::SwitchDeviceKind::Mux),    "Mux");
    EXPECT_EQ( core::meta::to_string( hal::SwitchDeviceKind::RfMux),  "RfMux");
}

TEST( HalSwitchFabric, ConnectAcceptsAPathMixingDeviceKinds)
{
    // hal::Path never distinguished device kinds -- it's just a vector of
    // SwitchElementId -- so a chain spanning mux hops and a matrix column in
    // one route needs no new mechanism at all. Nor would an RF chain, on a rig
    // that declared an RF selector.
    hal::SwitchFabric fabric;
    hal::Path path{
        { hal::SwitchDeviceId::Mux1,    3 },
        { hal::SwitchDeviceId::Mux2,    7 },
        { hal::SwitchDeviceId::Matrix2, 30 }
    };

    fabric.connect( path);

    for( const auto & element : path)
    {
        EXPECT_TRUE( fabric.isClosed( element));
    }

    fabric.disconnect( path);

    for( const auto & element : path)
    {
        EXPECT_FALSE( fabric.isClosed( element));
    }
}
