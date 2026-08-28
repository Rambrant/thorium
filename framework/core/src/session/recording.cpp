#include "core/session/recording.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
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
        // What a spilled payload's file is called after its content hash. An
        // extension at all so that the directory is navigable with ordinary
        // tools; ".bin" rather than something protocol-specific because a
        // payload is octets and this file knows nothing more about it than that.
        //
        constexpr std::string_view kBlobExtension = ".bin";

        //
        // A trace's samples get their own extension for the same reason: the
        // contents are doubles rather than protocol octets, and a directory
        // listing that says which is which is worth two characters.
        //
        constexpr std::string_view kTraceExtension = ".wfm";

        //
        // A number as text that reads back as the same number.
        //
        // std::format's default for a double is the shortest representation
        // that round-trips, which is exactly the requirement here and is not
        // what an ostream or core::formatNumber gives: both default to six
        // significant digits, so a rail reading of 5.0213456 V would be
        // recorded as 5.02135 and a replay would check a slightly different
        // number against the same criterion. Cheap to have been wrong about for
        // a voltage and impossible to accept for a timebase, where 1e-06 and
        // 1.0000005e-06 are different sample rates.
        //
        // Costs nothing in the common case: a value that is exactly 5 still
        // reads "5", not "5.0000000000000000".
        //
        [[nodiscard]]
        auto roundTripped( const double value) -> std::string
        {
            return std::format( "{}", value);
        }

        //
        // roundTripped's inverse, and strict where std::stod is not.
        //
        // The whole field has to be the number. from_chars stopping early means
        // there is something after it, and taking the prefix is how a corrupt
        // row becomes a plausible reading rather than an error: std::stod, which
        // this replaced, reads "5.021" out of "5.021<tab>1e-06<tab>..." and
        // discards the rest without a word. That is not a hypothetical -- it is
        // exactly what a row of the wrong width does when it lands in the wrong
        // columns, which is the one way a recording can be read wrong instead of
        // rejected.
        //
        // Same check as parseNumber in core/src/session/stimulus.cpp, which reads
        // hand-authored numbers and wanted it for the same reason.
        //
        [[nodiscard]]
        auto parsedNumber( const std::string & text, const std::string & line, const std::string_view what) -> double
        {
            auto value = 0.0;

            const auto [ end, error] = std::from_chars( text.data(), text.data() + text.size(), value);

            if( error != std::errc{} || end != text.data() + text.size())
            {
                throw std::runtime_error(
                    "readRecording: '" + text + "' is not a " + std::string( what) + ", in row '" + line + "'");
            }

            return value;
        }

        //
        // The integral half, strict for the same reason and against the same
        // hole: std::stoull( "0abc") is 0, silently.
        //
        template<typename IntegerT>
        [[nodiscard]]
        auto parsedInteger( const std::string & text, const std::string & line, const std::string_view what)
            -> IntegerT
        {
            auto value = IntegerT{};

            const auto [ end, error] = std::from_chars( text.data(), text.data() + text.size(), value);

            if( error != std::errc{} || end != text.data() + text.size())
            {
                throw std::runtime_error(
                    "readRecording: '" + text + "' is not a " + std::string( what) + ", in row '" + line + "'");
            }

            return value;
        }

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

        //
        // 64-bit FNV-1a. Not a cryptographic digest and not required to be --
        // see core/session/recording.hpp on why a collision here is caught rather than
        // designed out, and what the writer does when it finds one.
        //
        [[nodiscard]]
        auto contentHash( const Bytes & payload) -> std::uint64_t
        {
            constexpr std::uint64_t kOffsetBasis = 0xCBF29CE484222325ull;
            constexpr std::uint64_t kPrime       = 0x00000100000001B3ull;

            auto hash = kOffsetBasis;

            for( const auto octet : payload)
            {
                hash ^= static_cast<std::uint64_t>( octet);
                hash *= kPrime;
            }

            return hash;
        }

        [[nodiscard]]
        auto blobName( const Bytes & payload, const std::string_view extension) -> std::string
        {
            std::array<char, 17> digits{};

            std::snprintf( digits.data(), digits.size(), "%016llX",
                           static_cast<unsigned long long>( contentHash( payload)));

            return std::string( digits.data()) + std::string( extension);
        }

        //
        // The octets of one sidecar file. Binary, and read whole -- a blob is
        // the payload and nothing else, with no header and no encoding, which
        // is half the reason for having one.
        //
        [[nodiscard]]
        auto readBlob( const std::filesystem::path & path) -> Bytes
        {
            std::ifstream in( path, std::ios::binary);

            if( !in)
            {
                //
                // Not prefixed with a function name, unlike every other error
                // in this file: this is reached from the reader resolving a
                // reference *and* from the writer checking whether a name it is
                // about to take is already its own payload, and a message
                // naming the wrong one of those would send a reader to the
                // wrong half of the problem.
                //
                throw std::runtime_error( "could not open the sidecar payload '" + path.string() + "'");
            }

            //
            // Braces rather than parentheses: the parenthesised form of this
            // idiom is the most vexing parse, and declares a function.
            //
            const std::string content{
                std::istreambuf_iterator<char>( in),
                std::istreambuf_iterator<char>() };

            return Bytes( content);
        }

        //
        // A trace's samples as octets, and back -- see kSampleOctets in
        // core/session/recording.hpp for the encoding and why it is binary.
        //
        [[nodiscard]]
        auto packSamples( const std::span<const double> samples) -> Bytes
        {
            std::vector<std::byte> octets;

            octets.reserve( samples.size() * kSampleOctets);

            for( const auto sample : samples)
            {
                const auto bits = std::bit_cast<std::uint64_t>( sample);

                for( std::size_t i = 0; i < kSampleOctets; ++i)
                {
                    octets.push_back( static_cast<std::byte>( ( bits >> ( 8 * i)) & 0xFFu));
                }
            }

            return Bytes( std::move( octets));
        }

        [[nodiscard]]
        auto unpackSamples( const Bytes & octets, const std::string & line) -> std::vector<double>
        {
            if( octets.size() % kSampleOctets != 0)
            {
                throw std::runtime_error(
                    "readRecording: " + std::to_string( octets.size()) +
                    " octets is not a whole number of samples, in row '" + line + "'");
            }

            std::vector<double> samples;

            samples.reserve( octets.size() / kSampleOctets);

            for( std::size_t at = 0; at < octets.size(); at += kSampleOctets)
            {
                std::uint64_t bits = 0;

                for( std::size_t i = 0; i < kSampleOctets; ++i)
                {
                    bits |= static_cast<std::uint64_t>( octets.at( at + i)) << ( 8 * i);
                }

                samples.push_back( std::bit_cast<double>( bits));
            }

            return samples;
        }

        //
        // A sidecar reference names a file in the recording's own blob
        // directory and nothing else. Checked rather than trusted, because a
        // recording is a file read off disk: a row saying "@../../etc/passwd"
        // would otherwise have readRecording open whatever it named, and a
        // recording is exactly the kind of artifact that gets copied between
        // machines and mailed around after a failure.
        //
        auto checkBlobName( const std::string & name, const std::string & line) -> void
        {
            const auto path = std::filesystem::path( name);

            if( name.empty() || path.has_root_path() || path.has_parent_path() || name == "." || name == "..")
            {
                throw std::runtime_error(
                    "readRecording: '" + name + "' is not a sidecar payload name, in row '" + line + "'");
            }
        }
    } // namespace

    auto sidecarDirectoryFor( const std::filesystem::path & recording) -> std::filesystem::path
    {
        return recording.string() + ".d";
    }

    auto writeSelectionHeader(
        std::ostream &                         out,
        const std::vector<std::string_view> &  selection) -> void
    {
        out << kCommentMarker << ' ' << kSelectionField;

        if( selection.empty())
        {
            //
            // Written rather than left off, so that the line's absence means
            // "not a file this framework wrote" instead of being a third thing
            // to interpret. See kEverySelection.
            //
            out << kEverySelection;
        }
        else
        {
            auto separator = std::string_view{};

            for( const auto & test : selection)
            {
                out << separator << test;

                separator = ",";
            }
        }

        out << '\n';
    }

    auto RecordingWriter::spill( const Bytes & payload, const std::string_view extension) -> std::string
    {
        const auto name = blobName( payload, extension);
        const auto path = mSidecarDirectory / name;

        //
        // Created on the first payload that needs it rather than up front, so a
        // run that records nothing heavy leaves one file behind and no empty
        // directory beside it.
        //
        if( !mDirectoryReady)
        {
            std::filesystem::create_directories( mSidecarDirectory);

            mDirectoryReady = true;
        }

        if( std::filesystem::exists( path))
        {
            //
            // Either the same payload again -- the dedup this naming scheme
            // exists for -- or a hash collision, which is the one case that
            // must not be written through. See core/session/recording.hpp.
            //
            if( readBlob( path) == payload)
            {
                return kSidecarMarker + name;
            }

            throw std::runtime_error(
                "writeRecording: two different payloads hash to '" + name +
                "' -- the recording would replay the wrong one, so nothing was overwritten");
        }

        std::ofstream blob( path, std::ios::binary | std::ios::trunc);

        const auto octets = payload.data();

        blob.write( reinterpret_cast<const char *>( octets.data()), static_cast<std::streamsize>( octets.size()));
        blob.close();

        if( !blob)
        {
            throw std::runtime_error( "writeRecording: could not write the sidecar payload '" + path.string() + "'");
        }

        return kSidecarMarker + name;
    }

    auto RecordingWriter::valueColumn( const Bytes & payload, const std::string_view extension) -> std::string
    {
        //
        // Inline while it is small enough to read, a reference to a sidecar
        // file once it is not -- see core/session/recording.hpp on why the boundary
        // exists and why the reference names the payload's content. One rule
        // for a serial reply and for a trace's samples alike: they are the
        // same question, and two thresholds would be two answers to it.
        //
        const auto spillIt = payload.size() > kMaxInlinePayload && !mSidecarDirectory.empty();

        return spillIt ? spill( payload, extension) : unspacedHex( payload);
    }

    auto RecordingWriter::write( const RecordedSample & sample) -> void
    {
        mOut << sample.mSequence            << kSeparator
             << sample.mWallClockUnixMillis  << kSeparator
             << sample.mTestId              << kSeparator
             << sample.mPointName            << kSeparator
             << sample.mInstrumentId         << kSeparator;

        //
        // The kind column says which alternative the value column is in,
        // which is what makes a row self-describing on the way back in --
        // see kPayloadKind in core/session/recording.hpp on why the payload token
        // cannot collide with a unit's name.
        //
        if( const auto * payload = std::get_if<Bytes>( &sample.mValue))
        {
            mOut << kPayloadKind << kSeparator << valueColumn( *payload, kBlobExtension) << '\n';
        }
        else if( const auto * trace = std::get_if<Waveform>( &sample.mValue))
        {
            //
            // Three extra columns before the samples -- the unit they are in
            // and the timebase that gives them their positions. See kTraceKind
            // in core/session/recording.hpp on why they are in the row rather than in
            // the sample file.
            //
            mOut << kTraceKind                                     << kSeparator
                 << to_string( trace->kind())                      << kSeparator
                 << roundTripped( trace->timing().Origin.value())    << kSeparator
                 << roundTripped( trace->timing().Increment.value()) << kSeparator
                 << valueColumn( packSamples( trace->samples()), kTraceExtension) << '\n';
        }
        else if( const auto * flag = std::get_if<bool>( &sample.mValue))
        {
            //
            // "1"/"0" rather than "true"/"false": every other value column
            // in this file is a number, and a replay reads this back with
            // the same one-character test whichever spelling a future
            // writer might have preferred.
            //
            mOut << kFlagKind << kSeparator << ( *flag ? '1' : '0') << '\n';
        }
        else
        {
            const auto & value = std::get<QuantityVariant>( sample.mValue);

            mOut << to_string( static_cast<QuantityKind>( value.index())) << kSeparator
                 << roundTripped( rawValue( value)) << '\n';
        }
    }

    auto writeRecording(
        std::ostream &                       out,
        const std::vector<RecordedSample> &  samples,
        const std::filesystem::path &        sidecarDirectory) -> void
    {
        RecordingWriter writer( out, sidecarDirectory);

        for( const auto & sample : samples)
        {
            writer.write( sample);
        }
    }

    auto readRecording(
        std::istream &                 in,
        const std::filesystem::path &  sidecarDirectory) -> std::vector<RecordedSample>
    {
        std::vector<RecordedSample> samples;
        std::string                 line;

        while( std::getline( in, line))
        {
            //
            // Blank lines and comments are skipped. A recording this framework
            // wrote contains neither -- the comment syntax is here for the ones
            // it did not write: a --skeleton file explaining what its
            // placeholder values are, and a hand-authored replay saying what it
            // is standing in for. See core/session/recording.hpp.
            //
            if( line.empty() || line.front() == kCommentMarker)
            {
                continue;
            }

            std::istringstream fields( line);
            std::string        sequenceText, wallClockText, testId, pointName, instrumentId, kindText, valueText;

            if( !std::getline( fields, sequenceText,  kSeparator) ||
                !std::getline( fields, wallClockText, kSeparator) ||
                !std::getline( fields, testId,        kSeparator) ||
                !std::getline( fields, pointName,     kSeparator) ||
                !std::getline( fields, instrumentId,  kSeparator) ||
                !std::getline( fields, kindText,      kSeparator))
            {
                throw std::runtime_error( "readRecording: malformed row '" + line + "'");
            }

            //
            // Every column but the value has to carry something -- see
            // kRunScope in core/session/recording.hpp on why an observation outside any
            // test spells that rather than leaving this blank. Checked here
            // because a blank test column would otherwise reach the replay's
            // filter as an id no test has, and be silently dropped from a
            // selection it should have been exempt from.
            //
            if( testId.empty())
            {
                throw std::runtime_error( "readRecording: row '" + line + "' has no test id" );
            }

            //
            // A trace row carries its unit and timebase between the kind and
            // the samples -- see kTraceKind in core/session/recording.hpp on why they
            // are in the row. Read here so that the value column below is once
            // again simply "the last field", whichever kind of row this is.
            //
            std::string traceUnitText, traceOriginText, traceIncrementText;

            if( kindText == kTraceKind &&
                ( !std::getline( fields, traceUnitText,      kSeparator) ||
                  !std::getline( fields, traceOriginText,    kSeparator) ||
                  !std::getline( fields, traceIncrementText, kSeparator)))
            {
                throw std::runtime_error( "readRecording: malformed trace row '" + line + "'");
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
            if( !std::getline( fields, valueText) && kindText != kPayloadKind && kindText != kTraceKind)
            {
                throw std::runtime_error( "readRecording: malformed row '" + line + "'");
            }

            //
            // Resolves a payload column -- inline hex or a sidecar reference --
            // into the octets it stands for. Shared by a payload row and a
            // trace row's samples, which are encoded identically.
            //
            const auto octetsOf = [&]( const std::string & column) -> Bytes
            {
                if( !column.empty() && column.front() == kSidecarMarker)
                {
                    //
                    // A reference, not a value. The octets live beside the
                    // recording rather than in it -- see core/session/recording.hpp.
                    //
                    const auto name = column.substr( 1);

                    checkBlobName( name, line);

                    if( sidecarDirectory.empty())
                    {
                        throw std::runtime_error(
                            "readRecording: row '" + line +
                            "' refers to a sidecar payload, but no sidecar directory was given");
                    }

                    return readBlob( sidecarDirectory / name);
                }

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
                    return Bytes::fromHex( column);
                }
                catch( const std::invalid_argument & error)
                {
                    throw std::runtime_error(
                        "readRecording: malformed payload in row '" + line + "' -- " + error.what());
                }
            };

            auto value = RecordedValue{};

            if( kindText == kTraceKind)
            {
                //
                // parsedNumber for the timebase, as the quantity branch below
                // uses for its own number: a malformed one throws, which is
                // what a corrupt row should do -- and it has to be the whole
                // field, which is what stops a row of the wrong width being
                // read as a plausible one. See parsedNumber above.
                //
                value = Waveform{
                    quantityKindFromString( traceUnitText),
                    Waveform::Timing{
                        quantities::Time{ parsedNumber( traceOriginText,    line, "trace origin") },
                        quantities::Time{ parsedNumber( traceIncrementText, line, "trace increment") } },
                    unpackSamples( octetsOf( valueText), line) };
            }
            else if( kindText == kPayloadKind)
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
                value = octetsOf( valueText);
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

                value = quantityVariantFromKind( kind, parsedNumber( valueText, line, "value"));
            }

            samples.push_back( RecordedSample{
                .mSequence            = parsedInteger<std::uint64_t>( sequenceText,  line, "sequence number"),
                .mWallClockUnixMillis = parsedInteger<std::int64_t>(  wallClockText, line, "wall-clock time"),
                .mTestId              = testId,
                .mPointName           = pointName,
                .mInstrumentId        = instrumentId,
                .mValue               = std::move( value)
            });
        }

        return samples;
    }
} // namespace core
