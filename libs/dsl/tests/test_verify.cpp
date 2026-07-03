#include "dsl/verify.hpp"

#include <gtest/gtest.h>

#include "dsl/criterion.hpp"
#include "dsl/predicates.hpp"

// Exercise the GROUP/CRIT macros directly, the same way a real test script would.
GROUP(TestGroup, "A group used purely for unit-testing the macros")
    CRIT(SomeRegisterCheck, dsl::BAND(0x0Fu, 0x05u), "Low nibble must be 0x5")
END_GROUP

TEST(DslVerify, CriterionOverloadReturnsTrueOnPass) {
    EXPECT_TRUE(dsl::Verify(TestGroup::SomeRegisterCheck, 0xF5u));
}

TEST(DslVerify, CriterionOverloadReturnsFalseOnFail) {
    EXPECT_FALSE(dsl::Verify(TestGroup::SomeRegisterCheck, 0xF6u));
}

TEST(DslVerify, AdHocOverloadWorksWithoutCritMacro) {
    EXPECT_TRUE(dsl::Verify("Voltage", "Vout", "Supply voltage",
                            dsl::EQ(12.0).within(0.05), 12.03));
    EXPECT_FALSE(dsl::Verify("Voltage", "Vout", "Supply voltage",
                             dsl::EQ(12.0).within(0.05), 12.50));
}

TEST(DslCriterion, FieldsAreDeducedCorrectly) {
    EXPECT_EQ(TestGroup::SomeRegisterCheck.group, "TestGroup");
    EXPECT_EQ(TestGroup::SomeRegisterCheck.id, "SomeRegisterCheck");
    EXPECT_EQ(TestGroup::SomeRegisterCheck.description, "Low nibble must be 0x5");
}
