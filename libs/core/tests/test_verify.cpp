#include "core/verify.hpp"

#include <gtest/gtest.h>

#include "core/criterion.hpp"
#include "core/predicates.hpp"

//
// Exercise the GROUP/CRIT macros directly, the same way a real test script would.
//
GROUP( VerifyTestGroup, "A group used purely for unit-testing Verify")
    CRIT( SomeRegisterCheck, core::MASK(0x0Fu, 0x05u), "Low nibble must be 0x5")
END_GROUP

TEST( CoreVerify, CriterionOverloadReturnsTrueOnPass)
{
    EXPECT_TRUE( core::Verify( VerifyTestGroup::SomeRegisterCheck, 0xF5u));
}

TEST( CoreVerify, CriterionOverloadReturnsFalseOnFail)
{
    EXPECT_FALSE( core::Verify( VerifyTestGroup::SomeRegisterCheck, 0xF6u));
}

TEST( CoreVerify, AdHocOverloadWorksWithoutCritMacro)
{
    EXPECT_TRUE( core::Verify("Voltage", "Vout", "Supply voltage", core::EQ( 12.0).within( 0.05), 12.03));
    EXPECT_FALSE( core::Verify( "Voltage", "Vout", "Supply voltage", core::EQ( 12.0).within( 0.05), 12.50));
}