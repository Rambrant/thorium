//
// Moved here from framework/hal/tests/, alongside test_switch_device.cpp -- see that
// file's own note for the argument, which applies to this one with a twist
// worth stating.
//
// What this file tests genuinely is the mechanism: use-counting, connect and
// disconnect, a path composed from two independent wiring facts. None of that
// is about any one bench. But it cannot be exercised without device ids, and
// hal::SwitchDeviceId's enumerators come from the linking deployment's
// devices.inc -- so a bench with no switching hardware has nothing to build a
// SwitchElementId out of, and these tests have no inputs rather than failing
// inputs.
//
// Which is the honest reason this lands in rig/tests/ rather than being made
// deployment-agnostic: routing cannot be tested where there is nothing to
// route. A deployment with cards runs these; one without does not exercise the
// fabric at all, and should not pretend to. See dev/README.md on what that
// costs the dev deployment's coverage.
//
#include "hal/switch_fabric.hpp"

#include <gtest/gtest.h>

#include "core/meta.hpp"

TEST( HalSwitchFabric, CloseThenIsClosedReflectsState)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId ch{ hal::SwitchDeviceId::Matrix1, 0 };

    EXPECT_FALSE( fabric.isClosed( ch));

    fabric.close( ch);
    EXPECT_TRUE( fabric.isClosed( ch));

    fabric.open( ch);
    EXPECT_FALSE( fabric.isClosed( ch));
}

TEST( HalSwitchFabric, OpenAllClearsEveryElement)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId a{ hal::SwitchDeviceId::Matrix1, 0 };
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
    hal::SwitchElementId supplyOnly{ hal::SwitchDeviceId::Spst1, 0 };
    hal::SwitchElementId dmmOnly{ hal::SwitchDeviceId::Matrix1, 0 };

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
    hal::SwitchElementId supplyOnly{ hal::SwitchDeviceId::Spst1, 0 };
    hal::SwitchElementId dmmOnly{ hal::SwitchDeviceId::Matrix1, 0 };

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
    hal::SwitchElementId ch{ hal::SwitchDeviceId::Matrix1, 0 };

    fabric.open( ch); // never closed -- must not underflow or throw
    EXPECT_FALSE( fabric.isClosed( ch));
}

TEST( HalSwitchFabric, ToStringDescribesTheElement)
{
    hal::SwitchElementId ch{ hal::SwitchDeviceId::Matrix1, 0 };

    EXPECT_EQ( to_string( ch), "Matrix Matrix1 channel 0");
}

//
// A mux channel is an ordinary switch element, exactly as a matrix crosspoint
// is: nothing in SwitchFabric treats the kinds differently -- close/open/
// isClosed work identically whatever kind of card the channel is on.
//
// Written with an RfMux element again, which is the kind SwitchFabric is
// least likely to have been written with in mind. That spelling was
// impossible for a while and the reason is worth keeping: an element names a
// device the rig declared, and this rig declared no RF selector, so there was
// no way to construct one -- the claim moved to hal::SwitchDeviceKind, where
// it could still be made. rig/devices.inc now has an Agilent E1472A in it, so
// the claim can be made where it belongs again.
//
TEST( HalSwitchFabric, AnRfChannelIsAnOrdinarySwitchElementLikeAMatrixCrosspoint)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId mux{ hal::SwitchDeviceId::RfMux1, 21 };

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
    hal::SwitchElementId matrix{ hal::SwitchDeviceId::Matrix1, 300 };
    hal::SwitchElementId mux{ hal::SwitchDeviceId::Mux1, 2 };
    hal::SwitchElementId rf{ hal::SwitchDeviceId::RfMux1, 21 };

    EXPECT_EQ( to_string( matrix), "Matrix Matrix1 channel 300");
    EXPECT_EQ( to_string( mux),    "Mux Mux1 channel 2");
    EXPECT_EQ( to_string( rf),     "RfMux RfMux1 channel 21");

    EXPECT_EQ( hal::kindOf( hal::SwitchDeviceId::Matrix1), hal::SwitchDeviceKind::Matrix);
    EXPECT_EQ( hal::kindOf( hal::SwitchDeviceId::Mux1),    hal::SwitchDeviceKind::Mux);
    EXPECT_EQ( hal::kindOf( hal::SwitchDeviceId::RfMux1),  hal::SwitchDeviceKind::RfMux);
}

TEST( HalSwitchFabric, EveryDeviceKindRendersAsItsOwnName)
{
    // The bug this guards against: a kind silently printing as "Mux" because
    // to_string() only ever checked for Matrix and treated everything else as
    // Mux -- true right up until RfMux became a third kind, and now two kinds
    // further on. Made against the enum rather than through an element, so it
    // covers every kind hal has rather than every kind this rig happens to
    // own.
    EXPECT_EQ( core::meta::to_string( hal::SwitchDeviceKind::Matrix), "Matrix");
    EXPECT_EQ( core::meta::to_string( hal::SwitchDeviceKind::Mux),    "Mux");
    EXPECT_EQ( core::meta::to_string( hal::SwitchDeviceKind::RfMux),  "RfMux");
    EXPECT_EQ( core::meta::to_string( hal::SwitchDeviceKind::Spdt),   "Spdt");
    EXPECT_EQ( core::meta::to_string( hal::SwitchDeviceKind::Spst),   "Spst");
}

TEST( HalSwitchFabric, ConnectAcceptsAPathMixingDeviceKinds)
{
    // hal::Path never distinguished device kinds -- it's just a vector of
    // SwitchElementId -- so a chain spanning a mux hop, a changeover and a
    // matrix column in one route needs no new mechanism at all.
    hal::SwitchFabric fabric;
    hal::Path path{
        { hal::SwitchDeviceId::Mux1,    3 },
        { hal::SwitchDeviceId::Spdt1, 7 },
        { hal::SwitchDeviceId::Matrix1, 300 }
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
