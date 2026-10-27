//
// The host-side :MEASure, against waveforms whose answers are known. These
// are what the DSOX1202G's firmware does for it, and what nothing could check
// there: here every definition in measurements.hpp meets a trace built to have
// exactly one right answer.
//
#include "hal/pico_5244d/measurements.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

namespace m = hal::pico_5244d::measure;

namespace
{
    constexpr double kDt = 1e-6;

    //
    // `cycles` periods of a 0-to-3.3 V square wave, `period` samples each,
    // `high` of them high, with edges `edge` samples long (linear).
    //
    [[nodiscard]]
    auto square( const int cycles, const int period, const int high, const int edge = 0) -> std::vector<double>
    {
        std::vector<double> v;

        for( int c = 0; c < cycles; ++c)
        {
            for( int i = 0; i < period; ++i)
            {
                double level = i < high ? 3.3 : 0.0;

                if( edge > 0 && i < edge)
                {
                    level = 3.3 * ( i + 1) / ( edge + 1);                 // rising
                }
                else if( edge > 0 && i >= high && i < high + edge)
                {
                    level = 3.3 * ( 1.0 - ( i - high + 1.0) / ( edge + 1)); // falling
                }

                v.push_back( level);
            }
        }

        return v;
    }
} // namespace

TEST( Pico5244dMeasure, ExtremesAndMoments)
{
    const std::vector<double> v{ -1.0, 0.0, 1.0, 2.0 };

    EXPECT_DOUBLE_EQ( *m::vmax( v), 2.0);
    EXPECT_DOUBLE_EQ( *m::vmin( v), -1.0);
    EXPECT_DOUBLE_EQ( *m::vpp( v), 3.0);
    EXPECT_DOUBLE_EQ( *m::vaverage( v), 0.5);
    EXPECT_DOUBLE_EQ( *m::vrms( v), std::sqrt( 6.0 / 4.0));
}

TEST( Pico5244dMeasure, EmptyRecordSaysSo)
{
    const std::vector<double> none;

    ASSERT_FALSE( m::vpp( none));
    EXPECT_EQ( m::vpp( none).error(), "the record is empty");
    EXPECT_FALSE( m::period( none, kDt));
}

TEST( Pico5244dMeasure, SineRmsIsAmplitudeOverRootTwo)
{
    std::vector<double> v;

    for( int i = 0; i < 1000; ++i)
    {
        v.push_back( 2.0 * std::sin( 2.0 * std::numbers::pi * i / 100.0));   // ten whole cycles
    }

    EXPECT_NEAR( *m::vrms( v), 2.0 / std::sqrt( 2.0), 1e-9);
    EXPECT_NEAR( *m::vaverage( v), 0.0, 1e-9);
}

TEST( Pico5244dMeasure, TopAndBaseAreTheSettledLevelsNotTheExtremes)
{
    //
    // A square wave with one spike above and one below: vmax and vmin move,
    // vtop and vbase do not. That difference is the reason both exist -- a dip's
    // depth is vbase - vmin.
    //
    auto v = square( 10, 100, 50);

    v[ 20] = 7.0;
    v[ 70] = -2.0;

    EXPECT_DOUBLE_EQ( *m::vmax( v), 7.0);
    EXPECT_DOUBLE_EQ( *m::vmin( v), -2.0);
    EXPECT_NEAR( *m::vtop( v), 3.3, 1e-9);
    EXPECT_NEAR( *m::vbase( v), 0.0, 1e-9);
    EXPECT_NEAR( *m::vamplitude( v), 3.3, 1e-9);
}

TEST( Pico5244dMeasure, FlatRecordHasNoLevels)
{
    const std::vector<double> flat( 100, 1.5);

    ASSERT_FALSE( m::vbase( flat));
    EXPECT_EQ( m::vbase( flat).error(), "the record is flat: no top and base to tell apart");
    EXPECT_TRUE( m::vmax( flat));   // the extremes still exist
}

TEST( Pico5244dMeasure, PeriodAndFrequencyOfASquareWave)
{
    const auto v = square( 10, 100, 50);

    EXPECT_NEAR( *m::period( v, kDt), 100e-6, 1e-9);
    EXPECT_NEAR( *m::frequency( v, kDt), 10e3, 1e-3);
}

TEST( Pico5244dMeasure, OneEdgeIsNotAPeriod)
{
    std::vector<double> v( 50, 0.0);
    v.insert( v.end(), 50, 3.3);

    ASSERT_FALSE( m::period( v, kDt));
    EXPECT_EQ( m::period( v, kDt).error(), "fewer than two rising edges in the record");
}

TEST( Pico5244dMeasure, NoiseOnAnEdgeIsNotExtraEdges)
{
    //
    // Hysteresis: wobble of +/-0.2 V around the 50 % level of a 3.3 V swing
    // must still read as one period per cycle.
    //
    auto v = square( 10, 100, 50);

    for( std::size_t i = 0; i < v.size(); ++i)
    {
        const bool onEdge = ( i % 100 == 0) || ( i % 100 == 50);

        if( onEdge)
        {
            v[ i] = 1.65 + ( i % 200 == 0 ? 0.2 : -0.2);
        }
    }

    EXPECT_NEAR( *m::period( v, kDt), 100e-6, 2e-6);
}

TEST( Pico5244dMeasure, RiseAndFallTimesAreInterpolatedBetweenThresholds)
{
    // Edges ten samples long and linear: 10 % to 90 % of 3.3 V is 80 % of the
    // edge, 8 sample intervals, wherever the edge falls between samples.
    const auto v = square( 5, 200, 100, 10);

    EXPECT_NEAR( *m::riseTime( v, kDt, 0.1, 0.9), 0.8 * 11 * kDt, 0.05 * kDt);
    EXPECT_NEAR( *m::fallTime( v, kDt, 0.1, 0.9), 0.8 * 11 * kDt, 0.05 * kDt);
}

TEST( Pico5244dMeasure, ThresholdsAreTheScriptsToChoose)
{
    const auto v = square( 5, 200, 100, 10);

    // 20 % to 80 % is 60 % of the same edge.
    EXPECT_NEAR( *m::riseTime( v, kDt, 0.2, 0.8), 0.6 * 11 * kDt, 0.05 * kDt);
}

TEST( Pico5244dMeasure, NoEdgeMeansNoRiseTime)
{
    // A record that begins high and stays high has no rising edge to time.
    std::vector<double> v( 100, 3.3);
    v[ 0] = 0.0;
    v[ 1] = 0.0;
    v[ 2] = 3.3;

    ASSERT_TRUE( m::riseTime( v, kDt, 0.1, 0.9));   // there is one, at the very start

    std::vector<double> falling( 50, 3.3);
    falling.insert( falling.end(), 50, 0.0);

    ASSERT_FALSE( m::riseTime( falling, kDt, 0.1, 0.9));
    EXPECT_EQ( m::riseTime( falling, kDt, 0.1, 0.9).error(), "no complete edge between the two thresholds in the record");
    EXPECT_TRUE( m::fallTime( falling, kDt, 0.1, 0.9));
}

TEST( Pico5244dMeasure, PulseWidths)
{
    // 30 samples high, 70 low, repeating.
    const auto v = square( 10, 100, 30);

    EXPECT_NEAR( *m::positiveWidth( v, kDt), 30e-6, 1e-9);
    EXPECT_NEAR( *m::negativeWidth( v, kDt), 70e-6, 1e-9);
}

TEST( Pico5244dMeasure, ACutOffPulseHasNoWidth)
{
    // Rises once and never falls: the high pulse is cut off by the record.
    std::vector<double> v( 50, 0.0);
    v.insert( v.end(), 50, 3.3);

    ASSERT_FALSE( m::positiveWidth( v, kDt));
    EXPECT_EQ( m::positiveWidth( v, kDt).error(), "no complete high pulse in the record");
}
