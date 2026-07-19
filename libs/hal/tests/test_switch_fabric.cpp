#include "hal/switch_fabric.hpp"

#include <gtest/gtest.h>

TEST( HalSwitchFabric, CloseThenIsClosedReflectsState)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId ch{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };

    EXPECT_FALSE( fabric.isClosed( ch));

    fabric.close( ch);
    EXPECT_TRUE( fabric.isClosed( ch));

    fabric.open( ch);
    EXPECT_FALSE( fabric.isClosed( ch));
}

TEST( HalSwitchFabric, OpenAllClearsEveryElement)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId a{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };
    hal::SwitchElementId b{ hal::SwitchDeviceKind::Mux,    "Mux1",    3 };

    fabric.close( a);
    fabric.close( b);
    fabric.openAll();

    EXPECT_FALSE( fabric.isClosed( a));
    EXPECT_FALSE( fabric.isClosed( b));
}

TEST( HalSwitchFabric, ConnectTwiceThenDisconnectOnceLeavesTheSharedElementClosed)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId shared{ hal::SwitchDeviceKind::Mux, "Mux1", 6 };
    hal::SwitchElementId supplyOnly{ hal::SwitchDeviceKind::Matrix, "Matrix2", 22 };
    hal::SwitchElementId dmmOnly{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };

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
    hal::SwitchElementId shared{ hal::SwitchDeviceKind::Mux, "Mux1", 6 };
    hal::SwitchElementId supplyOnly{ hal::SwitchDeviceKind::Matrix, "Matrix2", 22 };
    hal::SwitchElementId dmmOnly{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };

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
    hal::SwitchElementId ch{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };

    fabric.open( ch); // never closed -- must not underflow or throw
    EXPECT_FALSE( fabric.isClosed( ch));
}

TEST( HalSwitchFabric, ToStringDescribesTheElement)
{
    hal::SwitchElementId ch{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };

    EXPECT_EQ( to_string( ch), "Matrix Matrix2 channel 14");
}

TEST( HalSwitchFabric, RfMuxIsAnOrdinarySwitchElementLikeMatrixOrMux)
{
    // RfMux is a genuinely separate physical device from Mux (see
    // hal::SwitchDeviceKind's own comment on why), but nothing in
    // SwitchFabric itself treats it any differently -- close/open/isClosed
    // work identically regardless of device kind.
    hal::SwitchFabric fabric;
    hal::SwitchElementId rf{ hal::SwitchDeviceKind::RfMux, "RfMux1", 2 };

    EXPECT_FALSE( fabric.isClosed( rf));

    fabric.close( rf);
    EXPECT_TRUE( fabric.isClosed( rf));

    fabric.open( rf);
    EXPECT_FALSE( fabric.isClosed( rf));
}

TEST( HalSwitchFabric, ToStringDistinguishesRfMuxFromMux)
{
    // The bug this guards against: an RfMux element silently printing as
    // "Mux" (or vice versa) because to_string() only ever checked for
    // Matrix and treated everything else as Mux -- true right up until
    // RfMux became a third kind.
    hal::SwitchElementId rf{ hal::SwitchDeviceKind::RfMux, "RfMux1", 2 };
    hal::SwitchElementId mux{ hal::SwitchDeviceKind::Mux, "Mux1", 2 };

    EXPECT_EQ( to_string( rf),  "RfMux RfMux1 channel 2");
    EXPECT_EQ( to_string( mux), "Mux Mux1 channel 2");
}

TEST( HalSwitchFabric, ConnectAcceptsAPathMixingAllThreeDeviceKinds)
{
    // hal::Path never distinguished device kinds -- it's just a vector of
    // SwitchElementId -- so a chain spanning matrix, mux, and RF mux hops
    // in one route (e.g. an RF signal narrowed down by an RfMux before
    // landing on a shared matrix column) needs no new mechanism at all.
    hal::SwitchFabric fabric;
    hal::Path path{
        { hal::SwitchDeviceKind::RfMux,  "RfMux1",  2 },
        { hal::SwitchDeviceKind::Mux,    "Mux1",    3 },
        { hal::SwitchDeviceKind::Matrix, "Matrix2", 30 }
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
