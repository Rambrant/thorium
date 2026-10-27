#include "hal/pico_5244d/measurements.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace hal::pico_5244d::measure
{
    namespace
    {
        constexpr std::string_view kEmpty = "the record is empty";
        constexpr std::string_view kFlat  = "the record is flat: no top and base to tell apart";

        // Enough to resolve a level to well under a tenth of a percent of the
        // range, and fewer bins than an 8-bit ADC has codes only when the range
        // is itself small -- where the bins are empty, not wrong.
        constexpr std::size_t kBins = 256;

        // Half the width of the band around the 50 % level that a signal must
        // leave before a new crossing counts, as a fraction of the amplitude.
        constexpr double kHysteresis = 0.1;

        struct Crossing
        {
            double Time{};
            bool   Rising{};
        };

        //
        // Where a straight line between two samples passes through `level`,
        // in sample units from `index`.
        //
        [[nodiscard]]
        auto fractionAt( const double before, const double after, const double level) -> double
        {
            const double span = after - before;

            return span == 0.0 ? 0.0 : ( level - before) / span;
        }

        //
        // The crossings of `level`, found with hysteresis: a crossing is
        // counted when the signal gets past level -/+ band, and *placed* where
        // it actually crossed `level`, by interpolation. The hysteresis is what
        // keeps a noisy edge from being one crossing per wobble; the
        // interpolation is what keeps the time from being quantised to the
        // sample interval.
        //
        [[nodiscard]]
        auto crossings( const std::span<const double> v, const double interval, const double level,
                        const double band) -> std::vector<Crossing>
        {
            std::vector<Crossing> found;

            enum class State { Unknown, Low, High };

            auto state = State::Unknown;

            for( std::size_t i = 0; i < v.size(); ++i)
            {
                const bool below = v[ i] <= level - band;
                const bool above = v[ i] >= level + band;

                if( !below && !above)
                {
                    continue;
                }

                const auto now = above ? State::High : State::Low;

                if( state != State::Unknown && now != state)
                {
                    //
                    // Walk back to the sample pair straddling the level. One
                    // exists: the previous state was the other side of the band.
                    //
                    std::size_t k = i;

                    while( k > 0 && ( now == State::High ? v[ k - 1] > level : v[ k - 1] < level))
                    {
                        --k;
                    }

                    const std::size_t from = k > 0 ? k - 1 : 0;
                    const double      at   = static_cast<double>( from) + fractionAt( v[ from], v[ from + 1], level);

                    found.push_back( Crossing{ at * interval, now == State::High });
                }

                state = now;
            }

            return found;
        }

        //
        // The time one edge takes between two absolute levels, on the first
        // edge that is a clean traverse: a sample at or below `low` followed
        // by one at or above `high`, measured from the *last* sample at or
        // below `low` before it, so a pre-edge wobble does not lengthen it.
        //
        [[nodiscard]]
        auto edgeTime( const std::span<const double> v, const double interval, const double low,
                       const double high) -> Result
        {
            std::size_t start = v.size();

            for( std::size_t i = 0; i < v.size(); ++i)
            {
                if( v[ i] <= low)
                {
                    start = i;
                }
                else if( start != v.size() && v[ i] >= high)
                {
                    // `start` is the last sample at or below low before here.
                    std::size_t lowAt = start;

                    while( lowAt + 1 < v.size() && v[ lowAt + 1] <= low)
                    {
                        ++lowAt;
                    }

                    std::size_t highAt = lowAt + 1;

                    while( highAt < v.size() && v[ highAt] < high)
                    {
                        ++highAt;
                    }

                    if( highAt >= v.size())
                    {
                        break;
                    }

                    const double tLow  = ( static_cast<double>( lowAt)
                                           + fractionAt( v[ lowAt], v[ lowAt + 1], low)) * interval;
                    const double tHigh = ( static_cast<double>( highAt - 1)
                                           + fractionAt( v[ highAt - 1], v[ highAt], high)) * interval;

                    return tHigh - tLow;
                }
            }

            return std::unexpected( "no complete edge between the two thresholds in the record");
        }
    } // namespace

    auto vmax( const std::span<const double> samples) -> Result
    {
        if( samples.empty())
        {
            return std::unexpected( kEmpty);
        }

        return *std::ranges::max_element( samples);
    }

    auto vmin( const std::span<const double> samples) -> Result
    {
        if( samples.empty())
        {
            return std::unexpected( kEmpty);
        }

        return *std::ranges::min_element( samples);
    }

    auto vpp( const std::span<const double> samples) -> Result
    {
        if( samples.empty())
        {
            return std::unexpected( kEmpty);
        }

        const auto [ least, most] = std::ranges::minmax_element( samples);

        return *most - *least;
    }

    auto vrms( const std::span<const double> samples) -> Result
    {
        if( samples.empty())
        {
            return std::unexpected( kEmpty);
        }

        const double squares = std::accumulate( samples.begin(), samples.end(), 0.0,
                                                []( const double sum, const double v) { return sum + v * v; });

        return std::sqrt( squares / static_cast<double>( samples.size()));
    }

    auto vaverage( const std::span<const double> samples) -> Result
    {
        if( samples.empty())
        {
            return std::unexpected( kEmpty);
        }

        return std::accumulate( samples.begin(), samples.end(), 0.0) / static_cast<double>( samples.size());
    }

    auto levels( const std::span<const double> samples) -> std::expected<Levels, std::string_view>
    {
        if( samples.empty())
        {
            return std::unexpected( kEmpty);
        }

        const auto [ leastIt, mostIt] = std::ranges::minmax_element( samples);
        const double least = *leastIt;
        const double most  = *mostIt;

        if( most <= least)
        {
            return std::unexpected( kFlat);
        }

        const double width = ( most - least) / static_cast<double>( kBins);

        std::vector<std::size_t> count( kBins, 0);
        std::vector<double>      sum( kBins, 0.0);

        for( const double v : samples)
        {
            const auto bin = std::min( kBins - 1, static_cast<std::size_t>( ( v - least) / width));

            ++count[ bin];
            sum[ bin] += v;
        }

        //
        // The fullest bin in each half of the range, first one winning a tie,
        // and its samples' mean rather than its centre: the bin is only how the
        // level was found, and the mean is not quantised to it.
        //
        const auto fullest = [ & ]( const std::size_t from, const std::size_t to)
        {
            std::size_t best = from;

            for( std::size_t bin = from; bin < to; ++bin)
            {
                if( count[ bin] > count[ best])
                {
                    best = bin;
                }
            }

            return sum[ best] / static_cast<double>( count[ best]);
        };

        return Levels{ fullest( 0, kBins / 2), fullest( kBins / 2, kBins) };
    }

    auto vbase( const std::span<const double> samples) -> Result
    {
        return levels( samples).transform( []( const Levels & l) { return l.Base; });
    }

    auto vtop( const std::span<const double> samples) -> Result
    {
        return levels( samples).transform( []( const Levels & l) { return l.Top; });
    }

    auto vamplitude( const std::span<const double> samples) -> Result
    {
        return levels( samples).transform( []( const Levels & l) { return l.Top - l.Base; });
    }

    auto period( const std::span<const double> samples, const double interval) -> Result
    {
        const auto level = levels( samples);

        if( !level)
        {
            return std::unexpected( level.error());
        }

        const double amplitude = level->Top - level->Base;
        const auto   found     = crossings( samples, interval, level->Base + amplitude / 2.0,
                                            amplitude * kHysteresis);

        std::vector<double> rising;

        for( const auto & crossing : found)
        {
            if( crossing.Rising)
            {
                rising.push_back( crossing.Time);
            }
        }

        if( rising.size() < 2)
        {
            return std::unexpected( "fewer than two rising edges in the record");
        }

        return ( rising.back() - rising.front()) / static_cast<double>( rising.size() - 1);
    }

    auto frequency( const std::span<const double> samples, const double interval) -> Result
    {
        return period( samples, interval).transform( []( const double seconds) { return 1.0 / seconds; });
    }

    auto riseTime( const std::span<const double> samples, const double interval, const double low,
                   const double high) -> Result
    {
        const auto level = levels( samples);

        if( !level)
        {
            return std::unexpected( level.error());
        }

        const double amplitude = level->Top - level->Base;

        return edgeTime( samples, interval, level->Base + low * amplitude, level->Base + high * amplitude);
    }

    auto fallTime( const std::span<const double> samples, const double interval, const double low,
                   const double high) -> Result
    {
        const auto level = levels( samples);

        if( !level)
        {
            return std::unexpected( level.error());
        }

        //
        // A falling edge is a rising one in the negated record, with its levels
        // negated and swapped -- one edge finder to be right, not two.
        //
        std::vector<double> negated( samples.size());

        std::ranges::transform( samples, negated.begin(), []( const double v) { return -v; });

        const double amplitude = level->Top - level->Base;

        return edgeTime( negated, interval, -( level->Base + high * amplitude), -( level->Base + low * amplitude));
    }

    namespace
    {
        [[nodiscard]]
        auto pulseWidth( const std::span<const double> samples, const double interval, const bool positive) -> Result
        {
            const auto level = levels( samples);

            if( !level)
            {
                return std::unexpected( level.error());
            }

            const double amplitude = level->Top - level->Base;
            const auto   found     = crossings( samples, interval, level->Base + amplitude / 2.0,
                                                amplitude * kHysteresis);

            //
            // The first crossing of the wanted polarity followed by one the
            // other way. A pulse cut off by either end of the record has no
            // width the record can state.
            //
            for( std::size_t i = 0; i + 1 < found.size(); ++i)
            {
                if( found[ i].Rising == positive)
                {
                    return found[ i + 1].Time - found[ i].Time;
                }
            }

            return std::unexpected( positive ? "no complete high pulse in the record"
                                             : "no complete low pulse in the record");
        }
    } // namespace

    auto positiveWidth( const std::span<const double> samples, const double interval) -> Result
    {
        return pulseWidth( samples, interval, true);
    }

    auto negativeWidth( const std::span<const double> samples, const double interval) -> Result
    {
        return pulseWidth( samples, interval, false);
    }
} // namespace hal::pico_5244d::measure
