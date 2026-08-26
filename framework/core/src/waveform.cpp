#include "core/waveform.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <string>
#include <variant>

#include "core/format.hpp"

namespace core
{
    namespace
    {
        //
        // Every reduction below is undefined on an empty trace, and all four
        // say so the same way -- see core::Waveform on why an empty capture is
        // a fault to stop on rather than a zero to check against a limit.
        //
        auto requireSamples( const Waveform & trace, const std::string_view reduction) -> void
        {
            if( trace.empty())
            {
                throw std::out_of_range(
                    "Waveform::" + std::string( reduction) + ": the trace is empty -- there is nothing to reduce");
            }
        }
    } // namespace

    auto Waveform::at( const std::size_t index) const -> double
    {
        if( index >= mSamples.size())
        {
            throw std::out_of_range(
                "Waveform::at: sample " + std::to_string( index) + " is past the end of a " +
                std::to_string( mSamples.size()) + "-sample trace");
        }

        return mSamples[ index];
    }

    auto Waveform::timeAt( const std::size_t index) const -> quantities::Time
    {
        //
        // Bounds-checked through at(), whose result is discarded: a time for a
        // sample that is not in the record is not a time, and a script indexing
        // past the end must find out here rather than being handed a plausible
        // number extrapolated off the end of the trace.
        //
        (void) at( index);

        return quantities::Time{
            mTiming.Origin.value() + static_cast<double>( index) * mTiming.Increment.value() };
    }

    auto Waveform::minimumValue() const -> QuantityVariant
    {
        requireSamples( *this, "minimum");

        return quantityVariantFromKind( mKind, *std::ranges::min_element( mSamples));
    }

    auto Waveform::maximumValue() const -> QuantityVariant
    {
        requireSamples( *this, "maximum");

        return quantityVariantFromKind( mKind, *std::ranges::max_element( mSamples));
    }

    auto Waveform::peakToPeakValue() const -> QuantityVariant
    {
        requireSamples( *this, "peakToPeak");

        const auto [ low, high] = std::ranges::minmax_element( mSamples);

        return quantityVariantFromKind( mKind, *high - *low);
    }

    auto Waveform::meanValue() const -> QuantityVariant
    {
        requireSamples( *this, "mean");

        const auto total = std::accumulate( mSamples.begin(), mSamples.end(), 0.0);

        return quantityVariantFromKind( mKind, total / static_cast<double>( mSamples.size()));
    }

    auto describeValue( const Waveform & value) -> std::string
    {
        if( value.empty())
        {
            return "0 pts";
        }

        //
        // Through the variant rather than off the raw double, so the summary
        // carries the unit symbol the rest of the log uses and cannot drift
        // from it -- describeValue for a Quantity<Unit> is what prints both.
        //
        const auto span = std::visit(
            []( const auto & quantity) { return describeValue( quantity); },
            value.peakToPeakValue());

        return std::to_string( value.size()) + " pts @ " +
               describeValue( value.timing().Increment) + ", " + span + " pk-pk";
    }
} // namespace core
