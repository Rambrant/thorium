#include "hal/vpc_location.hpp"

#include <gtest/gtest.h>

TEST( HalVpcLocation, EqualityComparesRackConnectorAndPin)
{
    hal::VpcLocation a{ hal::VpcRack::A, 1, 3 };
    hal::VpcLocation b{ hal::VpcRack::A, 1, 3 };
    hal::VpcLocation c{ hal::VpcRack::A, 1, 4 };
    hal::VpcLocation d{ hal::VpcRack::B, 1, 3 };

    EXPECT_EQ( a, b);
    EXPECT_NE( a, c);
    EXPECT_NE( a, d);
}

TEST( HalVpcLocation, ToStringMatchesTheRigNamingConvention)
{
    hal::VpcLocation a1_pin3{ hal::VpcRack::A, 1, 3 };

    EXPECT_EQ( to_string( a1_pin3), "A1/pin3");
}
