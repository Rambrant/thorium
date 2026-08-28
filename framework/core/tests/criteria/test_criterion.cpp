#include "core/criteria/criterion.hpp"

#include <gtest/gtest.h>

#include "core/criteria/predicates.hpp"

using namespace core::quantities;

//
// Exercise the CRITERIA/CRIT macros directly, the same way a test-spec table
// would be written.
//
CRITERIA( TestGroup, "A group used purely for unit-testing the macros")
    CRIT( SomeRegisterCheck, MASK( 0x0Fu, 0x05u), "Low nibble must be 0x5")
    CRIT( ExactValueCheck,   EQ( 0xF5u),          "Register must read 0xF5")
END_CRITERIA

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
        core::makeCriterion( "Adhoc", "AdhocCheck", "built by hand", EQ( 3.3).epsilon( 0.05));

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

//
// core::meta::all<Group>(): every CRIT in the group as a tuple, in
// declaration order -- proven at compile time, the same way the macro's own
// output is proven above.
//
static_assert( std::tuple_size_v<decltype( core::meta::all<TestGroup>())> == 2);
static_assert( std::get<0>( core::meta::all<TestGroup>()).id == "SomeRegisterCheck");
static_assert( std::get<1>( core::meta::all<TestGroup>()).id == "ExactValueCheck");

//
// core::meta::get<Group, "id">(): the one criterion named "id", looked up by
// reflecting over Group's members rather than spelled as Group::id directly
// -- for callers that only have the name as a string at the call site (e.g.
// built by another macro), not as a literal identifier to write by hand.
//
static_assert( core::meta::get<TestGroup, "ExactValueCheck">().id == "ExactValueCheck");
static_assert( core::meta::get<TestGroup, "ExactValueCheck">().predicate( 0xF5u));
static_assert( ! core::meta::get<TestGroup, "SomeRegisterCheck">().predicate( 0xF6u));

TEST( CoreCriterionMeta, AllReturnsEveryCriterionInDeclarationOrder)
{
    constexpr auto criteria = core::meta::all<TestGroup>();

    EXPECT_EQ( std::get<0>( criteria).id, "SomeRegisterCheck");
    EXPECT_EQ( std::get<1>( criteria).id, "ExactValueCheck");
}

TEST( CoreCriterionMeta, GetFindsCriterionByName)
{
    constexpr auto crit = core::meta::get<TestGroup, "ExactValueCheck">();

    EXPECT_EQ( crit.description, "Register must read 0xF5");
    EXPECT_TRUE( crit.predicate( 0xF5u));
    EXPECT_FALSE( crit.predicate( 0xF6u));
}

//
// COMPILE-TIME TYPO CHECK, reflection version: a misspelled name given to
// get<>() is a hard compile error (a failed static_assert inside get()),
// not a runtime failure -- there is no fallback path. Uncommenting the next
// line fails to compile with "no criterion with that id in this group":
//
//   constexpr auto oops = core::meta::get<TestGroup, "SomeRegisterChekc">();
//