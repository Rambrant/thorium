#include "core/verify.hpp"

#include <gtest/gtest.h>

#include "core/criterion.hpp"
#include "core/predicates.hpp"

using namespace core::quantities;

//
// Exercise the CRITERIA/CRIT macros directly, the same way a real test script would.
//
CRITERIA( VerifyTestGroup, "A group used purely for unit-testing Verify")
    CRIT( SomeRegisterCheck, MASK(0x0Fu, 0x05u), "Low nibble must be 0x5")
END_CRITERIA

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
    EXPECT_TRUE( core::Verify("Voltage", "Vout", "Supply voltage", EQ( 12.0).epsilon( 0.05), 12.03));
    EXPECT_FALSE( core::Verify( "Voltage", "Vout", "Supply voltage", EQ( 12.0).epsilon( 0.05), 12.50));
}

//
// The four-argument form: same check, no invented criteria group. The subject
// is what reaches the machine log's ruleId, so it is the argument that has to
// mean something; the description is prose beside it. See the overload's own
// comment in core/verify.hpp for why a group is not merely optional here but
// wrong to supply for a check that belongs to no table.
//
TEST( CoreVerify, GrouplessAdHocOverloadWorksWithoutCritMacro)
{
    EXPECT_TRUE(  core::Verify( "Vout", "Supply voltage", EQ( 12.0).epsilon( 0.05), 12.03));
    EXPECT_FALSE( core::Verify( "Vout", "Supply voltage", EQ( 12.0).epsilon( 0.05), 12.50));
}

//
// Unit safety is undiminished by dropping the group -- the four-argument form
// forwards to the very same Criterion overload, so a predicate in the wrong
// unit is still a compile error rather than a check that quietly compares
// numbers. (The negative half of that is in test_static_constraints.cpp; what
// this asserts is that the positive half still routes through.)
//
TEST( CoreVerify, GrouplessAdHocOverloadKeepsQuantityChecking)
{
    using namespace core::literals;

    EXPECT_TRUE(  core::Verify( "AcP1 phase voltage", "Primary AC at nominal",
                                EQ( 115_V).epsilon( 2_V), 114.2_V));
    EXPECT_FALSE( core::Verify( "AcP1 phase voltage", "Primary AC at nominal",
                                EQ( 115_V).epsilon( 2_V), 111.0_V));
}
