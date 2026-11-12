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
        // range. A record of fewer codes than this gets a bin per code instead
        // -- see levels().
        constexpr std::size_t kBins = 256;

        // How many times fuller than an even spread a histogram bin must be to
        // be a level (a square's plateau is hundreds; a sine's peak, ~20; a
        // ramp's noise-favoured bin, 1 to 3).
        constexpr double kPlateau = 5.0;

        // The share of its half's samples a bin holds to be a level whatever
        // kPlateau says -- see levels().
        constexpr double kMajority = 0.75;

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

        //
        // One bin per ADC code when the record spans fewer codes than kBins --
        // an 8-bit capture of a signal that fills a fifth of its range is fifty
        // codes. Spread over 256 bins those are fifty full bins among two
        // hundred empty ones, and every one of them is then many times fuller
        // than "an even spread": a ramp's every code passed for a plateau, and
        // its top was whichever code noise favoured.
        //
        // The code's size is the smallest step between neighbouring samples.
        // Samples are whole codes scaled, so no two differ by less; a record
        // that is not quantised at all has a step far below a bin's width, and
        // gets the kBins it always had.
        //
        double step = most - least;

        for( std::size_t i = 1; i < samples.size(); ++i)
        {
            const double gap = std::abs( samples[ i] - samples[ i - 1]);

            if( gap > 0.0 && gap < step)
            {
                step = gap;
            }
        }

        const double      codes   = ( most - least) / step;
        const bool        perCode = codes < static_cast<double>( kBins);
        const std::size_t bins    = perCode ? static_cast<std::size_t>( std::llround( codes)) + 1 : kBins;
        const double      width   = perCode ? step : ( most - least) / static_cast<double>( kBins);

        std::vector<std::size_t> count( bins, 0);
        std::vector<double>      sum( bins, 0.0);

        for( const double v : samples)
        {
            // Rounded to the nearest code, or cut into equal slices of the range.
            const double at  = ( v - least) / width;
            const auto   bin = std::min( bins - 1, static_cast<std::size_t>( perCode ? std::llround( at) : std::llround( std::floor( at))));

            ++count[ bin];
            sum[ bin] += v;
        }

        //
        // The fullest bin in each half of the range, first one winning a tie,
        // and its samples' mean rather than its centre: the bin is only how the
        // level was found, and the mean is not quantised to it.
        //
        //
        // But only a bin that is a plateau counts. A ramp or a triangle spends
        // equal time at every level, so its histogram is flat and the fullest
        // bin is whichever one noise favours -- which would shrink the
        // amplitude, and with it every edge time measured against it. A flat
        // half has no level of its own: its extreme is the level.
        //
        const auto fullest = [ & ]( const std::size_t from, const std::size_t to, const double extreme)
        {
            std::size_t best  = from;
            std::size_t total = 0;

            for( std::size_t bin = from; bin < to; ++bin)
            {
                total += count[ bin];

                if( count[ bin] > count[ best])
                {
                    best = bin;
                }
            }

            const double uniform = static_cast<double>( total) / static_cast<double>( to - from);

            //
            // Or, with too few bins in the half for any of them to be kPlateau
            // times an even share, a bin holding most of the half outright. A
            // triangle of two codes a half puts two thirds in the inner one,
            // which is the most a ramp manages; a square puts all but its
            // overshoot in one.
            //
            const bool plateau = static_cast<double>( count[ best]) >= kPlateau * uniform
                              || static_cast<double>( count[ best]) >= kMajority * static_cast<double>( total);

            if( !plateau)
            {
                return extreme;
            }

            return sum[ best] / static_cast<double>( count[ best]);
        };

        return Levels{ fullest( 0, bins / 2, least), fullest( bins / 2, bins, most) };
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

        //
        // A period from the edges of one direction, whichever the record holds
        // more of. Hysteresis makes an edge at either end of the record
        // invisible -- the signal has to clear the band before the crossing
        // counts -- so a record of exactly two periods triggered on a rising
        // edge at its centre holds only one countable rising edge, at the
        // centre, while its two falling edges sit safely half a period in.
        //
        std::vector<double> rising;
        std::vector<double> falling;

        for( const auto & crossing : found)
        {
            ( crossing.Rising ? rising : falling).push_back( crossing.Time);
        }

        const auto & edges = rising.size() >= falling.size() ? rising : falling;

        if( edges.size() < 2)
        {
            return std::unexpected( "fewer than two edges of either direction in the record");
        }

        return ( edges.back() - edges.front()) / static_cast<double>( edges.size() - 1);
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
