#include "core/recording.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace core
{
    namespace
    {
        constexpr char kSeparator = '\t';

        //
        // Bytes::hex() separates its octets with spaces, which is right for a
        // log line a human scans and wrong for a TSV field. Same digits, no
        // separator.
        //
        [[nodiscard]]
        auto unspacedHex( const Bytes & payload) -> std::string
        {
            auto text = payload.hex();

            std::erase( text, ' ');

            return text;
        }
    } // namespace

    auto writeRecording( std::ostream & out, const std::vector<RecordedSample> & samples) -> void
    {
        for( const auto & sample : samples)
        {
            out << sample.mSequence             << kSeparator
                << sample.mWallClockUnixMillis  << kSeparator
                << sample.mPointName            << kSeparator
                << sample.mInstrumentId         << kSeparator;

            //
            // The kind column says which alternative the value column is in,
            // which is what makes a row self-describing on the way back in --
            // see kPayloadKind in core/recording.hpp on why the payload token
            // cannot collide with a unit's name.
            //
            if( const auto * payload = std::get_if<Bytes>( &sample.mValue))
            {
                out << kPayloadKind << kSeparator << unspacedHex( *payload) << '\n';
            }
            else if( const auto * flag = std::get_if<bool>( &sample.mValue))
            {
                //
                // "1"/"0" rather than "true"/"false": every other value column
                // in this file is a number, and a replay reads this back with
                // the same one-character test whichever spelling a future
                // writer might have preferred.
                //
                out << kFlagKind << kSeparator << ( *flag ? '1' : '0') << '\n';
            }
            else
            {
                const auto & value = std::get<QuantityVariant>( sample.mValue);

                out << to_string( static_cast<QuantityKind>( value.index())) << kSeparator
                    << rawValue( value) << '\n';
            }
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
                !std::getline( fields, kindText,      kSeparator))
            {
                throw std::runtime_error( "readRecording: malformed row '" + line + "'");
            }

            //
            // The value is the last field, and the only one that is legitimately
            // allowed to be empty -- an empty payload is a real observation (a
            // port that answered nothing before its timeout came round), and it
            // has to read back as one rather than as a malformed row.
            //
            // getline fails rather than returning an empty string at end of
            // input, so the emptiness has to be recognised here instead of being
            // waved through by the check above: a quantity row with no number in
            // it is still malformed, and only a payload row may take this path.
            //
            if( !std::getline( fields, valueText) && kindText != kPayloadKind)
            {
                throw std::runtime_error( "readRecording: malformed row '" + line + "'");
            }

            auto value = RecordedValue{};

            if( kindText == kPayloadKind)
            {
                //
                // Translated, not propagated. Bytes::fromHex throws
                // std::invalid_argument, which is right for it -- a caller
                // passing bad hex made a programming error -- and wrong here: a
                // corrupt row in a file read off disk is a runtime condition,
                // and every other way this function rejects a row reports it as
                // a std::runtime_error naming the row. A consumer catching what
                // this function documents itself as throwing would otherwise
                // miss exactly the malformed input it was guarding against, and
                // the message it did get would name a stray character with no
                // indication of which row or which recording it came from.
                //
                try
                {
                    value = Bytes::fromHex( valueText);
                }
                catch( const std::invalid_argument & error)
                {
                    throw std::runtime_error(
                        "readRecording: malformed payload in row '" + line + "' -- " + error.what());
                }
            }
            else if( kindText == kFlagKind)
            {
                //
                // Only the two spellings writeRecording emits are accepted.
                // Anything else in this column is a corrupt row, not a truthy
                // value to be interpreted generously: a replay that silently
                // read "yes" or "" as false would report an acquisition that
                // never completed, and the checks beneath it would then be
                // failing for a reason the file does not contain.
                //
                if( valueText != "0" && valueText != "1")
                {
                    throw std::runtime_error( "readRecording: malformed flag in row '" + line + "'");
                }

                value = ( valueText == "1");
            }
            else
            {
                const auto kind = quantityKindFromString( kindText);

                value = quantityVariantFromKind( kind, std::stod( valueText));
            }

            samples.push_back( RecordedSample{
                .mSequence            = static_cast<std::uint64_t>( std::stoull( sequenceText)),
                .mWallClockUnixMillis = static_cast<std::int64_t>( std::stoll( wallClockText)),
                .mPointName           = pointName,
                .mInstrumentId        = instrumentId,
                .mValue               = std::move( value)
            });
        }

        return samples;
    }
} // namespace core
