//
// The driver API version gate (see hal/driver/api_version.hpp).
//
// What is testable here is the gate's logic, not a firing gate: a
// THORIUM_REQUIRE_HAL_API that fails stops the translation unit holding it from
// compiling, so a mismatched driver can never be a test case in a suite that
// has to build. What makes these tests worth more than that concession is that
// the macro is written in terms of the same two predicates these tests call --
// it repeats neither comparison -- so a change that broke the gate's meaning
// breaks these too.
//
#include "hal/driver/api_version.hpp"

#include <gtest/gtest.h>

TEST( HalApiVersion, TheTwoSpellingsOfEachNumberAgree)
{
    // Not a tautology waiting to be deleted: the constants are defined *from*
    // the macros, and this is what catches the two being separated. The macro is
    // what the gate asserts against and what a driver's #if reads; a constant
    // edited directly, with the macro left behind, would leave every driver
    // gated on the old number while everything else reported the new one.
    EXPECT_EQ( hal::kApiVersion, THORIUM_HAL_API_VERSION);
    EXPECT_EQ( hal::kOldestSupportedApiVersion, THORIUM_HAL_API_OLDEST_SUPPORTED);
}

TEST( HalApiVersion, TheSupportedRangeIsNotEmpty)
{
    // Oldest-supported above current means this hal supports no driver at all
    // and every gate in the tree fires at once. The way to get there is bumping
    // the oldest and forgetting the current, which is why this is an assert of
    // its own rather than left implicit in the ones below.
    EXPECT_LE( hal::kOldestSupportedApiVersion, hal::kApiVersion);
}

TEST( HalApiVersion, ThisHalSupportsItsOwnVersionAndTheOldestItClaims)
{
    EXPECT_TRUE( hal::apiVersionSupported( hal::kApiVersion));
    EXPECT_TRUE( hal::apiVersionSupported( hal::kOldestSupportedApiVersion));
}

TEST( HalApiVersion, ADriverWrittenAgainstANewerHalIsRejected)
{
    EXPECT_FALSE( hal::hasReachedApiVersion( hal::kApiVersion + 1));
    EXPECT_FALSE( hal::apiVersionSupported( hal::kApiVersion + 1));

    // The other half must not be the one objecting -- a driver from the future
    // is not a driver this hal has outgrown, and the two produce different
    // diagnostics. This is what pins each message to its own direction.
    EXPECT_TRUE( hal::stillSupportsApiVersion( hal::kApiVersion + 1));
}

TEST( HalApiVersion, ADriverWrittenAgainstADroppedApiIsRejected)
{
    EXPECT_FALSE( hal::stillSupportsApiVersion( hal::kOldestSupportedApiVersion - 1));
    EXPECT_FALSE( hal::apiVersionSupported( hal::kOldestSupportedApiVersion - 1));

    EXPECT_TRUE( hal::hasReachedApiVersion( hal::kOldestSupportedApiVersion - 1));
}

TEST( HalApiVersion, EveryVersionInTheClaimedRangeIsAccepted)
{
    // The boundary tests above would pass for a predicate accepting only the
    // two endpoints; a range is the claim actually being made. Trivially true
    // while the two numbers are equal, and it stops being trivial the first
    // time hal keeps compatibility across a bump -- which is the case the
    // whole two-number scheme exists for.
    for( int version = hal::kOldestSupportedApiVersion; version <= hal::kApiVersion; ++version)
    {
        EXPECT_TRUE( hal::apiVersionSupported( version))
            << "hal claims to support API version " << version << " and its own gate rejects it";
    }
}

TEST( HalApiVersion, EveryDriverInThisTreeIsGatedAtAVersionThisHalServes)
{
    // Deliberately not a list of driver names: generic hal must not know its
    // drivers (see framework/hal/CMakeLists.txt on the two-target split), and this
    // test links plain hal, so it could not name one anyway. What it asserts is
    // the number the five drivers in instruments/ currently pass to
    // THORIUM_REQUIRE_HAL_API -- and if that literal ever falls out of this
    // hal's range, those drivers stop compiling and say so themselves. This is
    // the reminder to update them in the same commit rather than discovering it
    // from the driver targets.
    EXPECT_TRUE( hal::apiVersionSupported( 1));
}
