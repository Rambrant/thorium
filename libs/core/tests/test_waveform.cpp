#include "core/waveform.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "core/quantity.hpp"
#include "core/journal.hpp"
#include "core/quantity_kind.hpp"

//
// core::Waveform -- the captured record, and the reductions a script checks
// instead of the trace itself (see that class's own comment on why there is no
// criterion that takes one).
//

using core::Waveform;
using core::quantities::Current;
using core::quantities::Time;
using core::quantities::Voltage;

namespace
{
    //
    // A 5 V rail that dips to 4.6 V in the middle -- the shape every claim
    // below is about, and the shape the whole verb exists for.
    //
    auto dip() -> Waveform
    {
        std::vector<double> samples( 10, 5.0);

        samples[ 4] = 4.6;
        samples[ 5] = 4.8;

        return Waveform{
            core::quantityKindOf<Voltage>(),
            Waveform::Timing{ Time{ -0.001 }, Time{ 0.0001 } },
            std::move( samples) };
    }
} // namespace

TEST( CoreWaveform, ADefaultTraceIsEmpty)
{
    EXPECT_TRUE( Waveform{}.empty());
    EXPECT_EQ( Waveform{}.size(), 0u);
}

TEST( CoreWaveform, TheReductionsComeBackInTheTracesOwnUnit)
{
    const auto trace = dip();

    EXPECT_DOUBLE_EQ( trace.minimum<Voltage>().value(),    4.6);
    EXPECT_DOUBLE_EQ( trace.maximum<Voltage>().value(),    5.0);
    EXPECT_NEAR( trace.peakToPeak<Voltage>().value(), 0.4, 1e-12);

    // Eight samples at 5.0, one at 4.6, one at 4.8.
    EXPECT_DOUBLE_EQ( trace.mean<Voltage>().value(), ( 8 * 5.0 + 4.6 + 4.8) / 10.0);
}

TEST( CoreWaveform, AskingForTheWrongUnitIsAnErrorNamingIt)
{
    //
    // The same crossing every other runtime-tagged value makes into a concrete
    // Quantity<Unit>, and the same guarantee: a current trace cannot be read as
    // a voltage by accident.
    //
    try
    {
        (void) dip().minimum<Current>();

        ADD_FAILURE() << "a voltage trace should not answer a current";
    }
    catch( const std::runtime_error & error)
    {
        EXPECT_NE( std::string( error.what()).find( "Voltage"), std::string::npos);
    }
}

TEST( CoreWaveform, ThereIsNoMinimumOfNothing)
{
    //
    // An empty capture is a bench fault to stop on, not a zero to check against
    // a limit -- see core::Waveform.
    //
    EXPECT_THROW( (void) Waveform{}.minimum<Voltage>(),    std::out_of_range);
    EXPECT_THROW( (void) Waveform{}.maximum<Voltage>(),    std::out_of_range);
    EXPECT_THROW( (void) Waveform{}.peakToPeak<Voltage>(), std::out_of_range);
    EXPECT_THROW( (void) Waveform{}.mean<Voltage>(),       std::out_of_range);
}

TEST( CoreWaveform, SamplePositionsAreTimesRelativeToTheTrigger)
{
    const auto trace = dip();

    // Origin is negative: the record starts before the event that caused it.
    EXPECT_DOUBLE_EQ( trace.timeAt( 0).value(), -0.001);
    EXPECT_DOUBLE_EQ( trace.timeAt( 4).value(), -0.001 + 4 * 0.0001);
}

TEST( CoreWaveform, ReadingPastTheEndIsAnErrorRatherThanAnExtrapolation)
{
    //
    // Both accessors, because a plausible time for a sample that is not in the
    // record is the more dangerous of the two answers.
    //
    EXPECT_THROW( (void) dip().at( 10),     std::out_of_range);
    EXPECT_THROW( (void) dip().timeAt( 10), std::out_of_range);
}

TEST( CoreWaveform, ALogLineSummarisesTheTraceRatherThanSpellingItOut)
{
    const auto described = core::describeValue( dip());

    EXPECT_NE( described.find( "10 pts"),  std::string::npos) << described;
    EXPECT_NE( described.find( "pk-pk"),   std::string::npos) << described;

    //
    // Bounded by construction rather than by a limit: there is no length of
    // trace at which this line grows.
    //
    EXPECT_LT( described.size(), core::kMaxJournalValueLength);
}
