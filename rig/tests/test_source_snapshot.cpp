#include "hal/verbs/snapshot.hpp"

#include "hal/keysight_ac6834b.hpp"
#include "hal/keysight_edu36311a.hpp"
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/safing.hpp"

#include <gtest/gtest.h>

#include <type_traits>

using namespace core::literals;

//
// hal::SourceSnapshot against this rig's own sources -- the mechanism the
// suite's two group brackets are wrappers around. suite/tests/ exercises it
// through those hooks and the power-up sequence; what is pinned down here is
// the mechanism's own rules, which do not need a suite: nothing is restored
// that was not captured, nothing that was not energised, and nothing that is
// still on.
//
namespace
{
    using DcRelay  = std::remove_cvref_t<decltype( DcP6)>;
    using DcDirect = std::remove_cvref_t<decltype( DcP5)>;
    using Ac       = std::remove_cvref_t<decltype( AcP1)>;

    //
    // The rig starts each test dark, and ends it dark -- GoogleTest runs the
    // whole file in one process and a live source must not leak into the next
    // fixture, here or in any other file linked into rig_tests.
    //
    struct SourceSnapshotFixture : ::testing::Test
    {
        void SetUp() override    { hal::safeRig(); }
        void TearDown() override { hal::safeRig(); }

        static auto energiseDcP6( const core::quantities::Voltage voltage) -> void
        {
            Connect( DcP6.dc());
            Apply(   DcP6.dc().voltage( voltage).currentLimit( 1_A));
        }

        static auto dropDcP6() -> void
        {
            Remove(     DcP6.dc());
            Disconnect( DcP6.dc());
        }
    };
} // namespace

TEST_F( SourceSnapshotFixture, RestoresASourceTheScriptTookAway)
{
    energiseDcP6( 24_V);

    hal::SourceSnapshot<DcRelay> snapshot;
    snapshot.capture( DcP6);

    dropDcP6();
    ASSERT_FALSE( DcP6.isEnabled());

    EXPECT_TRUE( snapshot.restore( DcP6));

    EXPECT_TRUE( DcP6.isEnabled());
    EXPECT_EQ( DcP6.outputVoltage(), 24_V);
    EXPECT_EQ( DcP6.currentLimit(), std::optional{ 1_A});
}

TEST_F( SourceSnapshotFixture, DoesNothingWhenNothingWasCaptured)
{
    energiseDcP6( 24_V);
    dropDcP6();

    const hal::SourceSnapshot<DcRelay> snapshot;

    EXPECT_FALSE( snapshot.restore( DcP6));
    EXPECT_FALSE( DcP6.isEnabled());
}

TEST_F( SourceSnapshotFixture, DoesNotEnergiseASourceThatWasNeverOn)
{
    hal::SourceSnapshot<DcRelay> snapshot;
    snapshot.capture( DcP6);

    ASSERT_FALSE( DcP6.isEnabled());

    EXPECT_FALSE( snapshot.restore( DcP6));
    EXPECT_FALSE( DcP6.isEnabled());
}

TEST_F( SourceSnapshotFixture, LeavesASourceThatIsStillOnAlone)
{
    energiseDcP6( 24_V);

    hal::SourceSnapshot<DcRelay> snapshot;
    snapshot.capture( DcP6);

    EXPECT_FALSE( snapshot.restore( DcP6));
}

TEST_F( SourceSnapshotFixture, TheSecondCaptureReplacesTheFirst)
{
    energiseDcP6( 24_V);

    hal::SourceSnapshot<DcRelay> snapshot;
    snapshot.capture( DcP6);

    energiseDcP6( 12_V);
    snapshot.capture( DcP6);

    dropDcP6();

    EXPECT_TRUE( snapshot.restore( DcP6));
    EXPECT_EQ( DcP6.outputVoltage(), 12_V);
}

TEST_F( SourceSnapshotFixture, RestoresAThreePhaseSource)
{
    Connect( AcP1.ac());
    Apply(   AcP1.ac().phaseVoltage( 115_V).frequency( 400_Hz).currentLimit( 2_A));

    hal::SourceSnapshot<Ac> snapshot;
    snapshot.capture( AcP1);

    Remove(     AcP1.ac());
    Disconnect( AcP1.ac());
    ASSERT_FALSE( AcP1.isEnabled());

    EXPECT_TRUE( snapshot.restore( AcP1));

    EXPECT_TRUE( AcP1.isEnabled());
    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::C), 115_V);
    EXPECT_EQ( AcP1.frequency(), std::optional{ 400_Hz});
}

//
// A DirectWiring output has no relay to close, so a restore of one must compile
// without a Connect -- the constraint on connectDriver is what selects the
// branch, and this is the instantiation that exercises the other one. Nothing
// to assert at run time; the point is that it builds.
//
TEST_F( SourceSnapshotFixture, ASourceWithNoRelayRestoresWithoutAConnect)
{
    hal::SourceSnapshot<DcDirect> snapshot;
    snapshot.capture( DcP5);

    EXPECT_FALSE( snapshot.restore( DcP5));
}
