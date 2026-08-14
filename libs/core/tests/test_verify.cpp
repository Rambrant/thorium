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

TEST( CoreVerify, AdHocOverloadWithAnExplicitGroupAndIdWorksWithoutCritMacro)
{
    EXPECT_TRUE(  core::Verify( "Voltage", "Vout", "Supply voltage", EQ( 12.0).epsilon( 0.05), 12.03));
    EXPECT_FALSE( core::Verify( "Voltage", "Vout", "Supply voltage", EQ( 12.0).epsilon( 0.05), 12.50));
}

//
// The three-argument form: one line of prose, the predicate, the value -- no
// group and no id, because an ad-hoc check belongs to no criteria table and has
// no identity to track between runs. See the overload's own comment in
// core/verify.hpp, and CoreSarifRuleId below for what its results index under.
//
TEST( CoreVerify, AdHocOverloadNeedsNothingButProse)
{
    EXPECT_TRUE(  core::Verify( "Supply voltage at Vout", EQ( 12.0).epsilon( 0.05), 12.03));
    EXPECT_FALSE( core::Verify( "Supply voltage at Vout", EQ( 12.0).epsilon( 0.05), 12.50));
}

//
// Unit safety is undiminished by dropping the identifiers -- the three-argument
// form forwards to the very same Criterion overload, so a predicate in the
// wrong unit is still a compile error rather than a check that quietly compares
// numbers. (The negative half of that is in test_static_constraints.cpp; what
// this asserts is that the positive half still routes through.)
//
TEST( CoreVerify, AdHocOverloadKeepsQuantityChecking)
{
    using namespace core::literals;

    EXPECT_TRUE(  core::Verify( "Primary AC at nominal", EQ( 115_V).epsilon( 2_V), 114.2_V));
    EXPECT_FALSE( core::Verify( "Primary AC at nominal", EQ( 115_V).epsilon( 2_V), 111.0_V));
}
