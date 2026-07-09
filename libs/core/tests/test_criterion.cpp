#include "core/criterion.hpp"

#include <gtest/gtest.h>

#include "core/predicates.hpp"

//
// Exercise the GROUP/CRIT macros directly, the same way a test-spec table
// would be written.
//
GROUP( TestGroup, "A group used purely for unit-testing the macros")
    CRIT( SomeRegisterCheck, core::MASK( 0x0Fu, 0x05u), "Low nibble must be 0x5")
    CRIT( ExactValueCheck,   core::EQ( 0xF5u),          "Register must read 0xF5")
END_GROUP

//
// The whole table is a compile-time constant: prove the criteria are usable in
// constant expressions, and that their metadata is stamped correctly.
//
static_assert( TestGroup::Name == "TestGroup");
static_assert( TestGroup::SomeRegisterCheck.group == "TestGroup");
static_assert( TestGroup::SomeRegisterCheck.id == "SomeRegisterCheck");
static_assert( TestGroup::SomeRegisterCheck.predicate( 0xF5u));
static_assert( ! TestGroup::SomeRegisterCheck.predicate( 0xF6u));

//
// The predicate held by a criterion satisfies the framework's PredicateFor
// concept for the value type it is meant to check.
//
static_assert( core::PredicateFor< decltype( TestGroup::ExactValueCheck.predicate),
                                   unsigned>);

TEST( CoreCriterion, FieldsAreStampedFromMacro)
{
    EXPECT_EQ( TestGroup::SomeRegisterCheck.group, "TestGroup");
    EXPECT_EQ( TestGroup::SomeRegisterCheck.id, "SomeRegisterCheck");
    EXPECT_EQ( TestGroup::SomeRegisterCheck.description, "Low nibble must be 0x5");
}

TEST( CoreCriterion, PredicateEvaluatesThroughCriterion)
{
    EXPECT_TRUE( TestGroup::SomeRegisterCheck.predicate( 0xF5u));
    EXPECT_FALSE( TestGroup::SomeRegisterCheck.predicate( 0xF6u));
    EXPECT_TRUE( TestGroup::ExactValueCheck.predicate( 0xF5u));
}

TEST( CoreCriterion, MakeCriterionBuildsByHand)
{
    constexpr auto crit =
        core::makeCriterion( "Adhoc", "AdhocCheck", "built by hand", core::EQ( 3.3).within( 0.05));

    EXPECT_EQ( crit.id, "AdhocCheck");
    EXPECT_TRUE( crit.predicate( 3.32));
    EXPECT_FALSE( crit.predicate( 3.5));
}

//
// COMPILE-TIME TYPO CHECK (by design): referencing a misspelled criterion is a
// compile error, because CRIT declares each criterion as a named member.
// Uncommenting the next line fails to compile with "no member named
// 'SomeRegisterChekc' in 'TestGroup'":
//
//   auto oops = TestGroup::SomeRegisterChekc;
//
