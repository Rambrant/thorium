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

TEST( HalSwitchFabric, RouteOpensEverythingElseFirst)
{
    hal::SwitchFabric fabric;
    hal::SwitchElementId stale{ hal::SwitchDeviceKind::Mux, "Mux3", 1 };
    hal::SwitchElementId a{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };
    hal::SwitchElementId b{ hal::SwitchDeviceKind::Mux,    "Mux1",    3 };

    fabric.close( stale);
    fabric.route( { a, b });

    EXPECT_FALSE( fabric.isClosed( stale));
    EXPECT_TRUE( fabric.isClosed( a));
    EXPECT_TRUE( fabric.isClosed( b));
}

TEST( HalSwitchFabric, ToStringDescribesTheElement)
{
    hal::SwitchElementId ch{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };

    EXPECT_EQ( to_string( ch), "Matrix Matrix2 channel 14");
}
