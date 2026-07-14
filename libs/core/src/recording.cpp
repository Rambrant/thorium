#include "core/recording.hpp"

#include <sstream>
#include <stdexcept>

namespace core
{
    namespace
    {
        constexpr char kSeparator = '\t';
    } // namespace

    auto writeRecording( std::ostream & out, const std::vector<RecordedSample> & samples) -> void
    {
        for( const auto & sample : samples)
        {
            out << sample.mSequence            << kSeparator
                << sample.mWallClockUnixMillis  << kSeparator
                << sample.mPointName            << kSeparator
                << sample.mInstrumentId         << kSeparator
                << to_string( sample.mKind)     << kSeparator
                << rawValue( sample.mValue)     << '\n';
        }
    }

    auto readRecording( std::istream & in) -> std::vector<RecordedSample>
    {
        std::vector<RecordedSample> samples;
        std::string                 line;

        while( std::getline( in, line))
        {
            if( line.empty())
            {
                continue;
            }

            std::istringstream fields( line);
            std::string        sequenceText, wallClockText, pointName, instrumentId, kindText, valueText;

            if( !std::getline( fields, sequenceText,  kSeparator) ||
                !std::getline( fields, wallClockText, kSeparator) ||
                !std::getline( fields, pointName,     kSeparator) ||
                !std::getline( fields, instrumentId,  kSeparator) ||
                !std::getline( fields, kindText,      kSeparator) ||
                !std::getline( fields, valueText))
            {
                throw std::runtime_error( "readRecording: malformed row '" + line + "'");
            }

            const auto kind = quantityKindFromString( kindText);

            samples.push_back( RecordedSample{
                .mSequence            = static_cast<std::uint64_t>( std::stoull( sequenceText)),
                .mWallClockUnixMillis = static_cast<std::int64_t>( std::stoll( wallClockText)),
                .mPointName           = pointName,
                .mInstrumentId        = instrumentId,
                .mKind                = kind,
                .mValue               = quantityVariantFromKind( kind, std::stod( valueText))
            });
        }

        return samples;
    }
} // namespace core
