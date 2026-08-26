#include "core/stimulus.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <variant>
#include <vector>

#include "core/bytes.hpp"
#include "core/meta.hpp"
#include "core/waveform.hpp"

namespace core
{
    namespace
    {
        //
        // One parsed value, in the same four alternatives an observation can
        // take -- see core::RecordedValue, which this deliberately mirrors. A
        // line's values must all be the same alternative, which is what makes
        // "which seam is this key programmed on" answerable from the value's
        // shape alone.
        //
        using StimulusValue = std::variant<QuantityVariant, Bytes, bool, Waveform>;

        [[nodiscard]]
        auto trimmed( std::string_view text) -> std::string_view
        {
            const auto isSpace = []( const char c) { return std::isspace( static_cast<unsigned char>( c)) != 0; };

            while( !text.empty() && isSpace( text.front())) { text.remove_prefix( 1); }
            while( !text.empty() && isSpace( text.back()))  { text.remove_suffix( 1); }

            return text;
        }

        //
        // Everything before an unquoted '#'. Quote-aware, because a payload is
        // free to contain one: Ser1.Data = "#3" is a reply, not a comment.
        //
        [[nodiscard]]
        auto withoutComment( const std::string_view line) -> std::string_view
        {
            auto quoted = false;

            for( std::size_t i = 0; i < line.size(); ++i)
            {
                if( line[ i] == '"' && ( i == 0 || line[ i - 1] != '\\'))
                {
                    quoted = !quoted;
                }
                else if( line[ i] == '#' && !quoted)
                {
                    return line.substr( 0, i);
                }
            }

            return line;
        }

        //
        // Splits at top-level commas only -- a comma inside a payload, a trace's
        // argument list or an inline sample list belongs to that value.
        //
        // Depth-counted rather than parsed properly, which is enough because
        // every construct here is delimited and none of them nest ambiguously.
        //
        [[nodiscard]]
        auto splitValues( const std::string_view text) -> std::vector<std::string_view>
        {
            std::vector<std::string_view>  values;
            std::size_t                    start = 0;
            int                            depth = 0;
            auto                           quoted = false;

            for( std::size_t i = 0; i < text.size(); ++i)
            {
                const auto c = text[ i];

                if( c == '"' && ( i == 0 || text[ i - 1] != '\\'))
                {
                    quoted = !quoted;
                }
                else if( !quoted && ( c == '(' || c == '[' || c == '<'))
                {
                    ++depth;
                }
                else if( !quoted && ( c == ')' || c == ']' || c == '>'))
                {
                    --depth;
                }
                else if( c == ',' && !quoted && depth == 0)
                {
                    values.push_back( trimmed( text.substr( start, i - start)));
                    start = i + 1;
                }
            }

            values.push_back( trimmed( text.substr( start)));

            return values;
        }

        [[nodiscard]]
        auto parseNumber( const std::string_view text, const std::string_view what) -> double
        {
            auto value = 0.0;

            const auto [ end, error] = std::from_chars( text.data(), text.data() + text.size(), value);

            //
            // The whole token has to be the number. from_chars stopping early
            // means there is something after it -- a stray unit, a typo, a
            // second number -- and accepting the prefix would silently take
            // "5.01 VV" for five volts.
            //
            if( error != std::errc{} || end != text.data() + text.size())
            {
                throw std::runtime_error( "'" + std::string( text) + "' is not a number (" + std::string( what) + ")");
            }

            return value;
        }

        //
        // "5.01 V" -> a Voltage. The number and the symbol, with the symbol
        // optional: nothing after the number is the dimensionless kind, whose
        // symbol genuinely is the empty string (see core/quantity.hpp).
        //
        [[nodiscard]]
        auto parseQuantity( const std::string_view text) -> QuantityVariant
        {
            const auto split  = text.find_first_of( " \t");
            const auto number = trimmed( text.substr( 0, split));
            const auto symbol = split == std::string_view::npos ? std::string_view{} : trimmed( text.substr( split));

            const auto kind = quantityKindFromSymbol( symbol);

            if( !kind)
            {
                throw std::runtime_error( "'" + std::string( symbol) + "' is not a unit symbol");
            }

            return quantityVariantFromKind( *kind, parseNumber( number, "a reading"));
        }

        //
        // "ACK\r" -> the octets. The escapes are describeValue's, and only
        // those: a stimulus file spells a payload the way a log line spells it,
        // so a reader can paste one into the other, and an escape this
        // understands but describeValue never writes would break that in the
        // one direction that matters.
        //
        [[nodiscard]]
        auto parseTextPayload( const std::string_view text) -> Bytes
        {
            std::string body;

            for( std::size_t i = 1; i + 1 < text.size(); ++i)
            {
                if( text[ i] != '\\')
                {
                    body += text[ i];
                    continue;
                }

                if( ++i + 1 > text.size())
                {
                    throw std::runtime_error( "'" + std::string( text) + "' ends in a half-written escape");
                }

                switch( text[ i])
                {
                    case 'r':  body += '\r';  break;
                    case 'n':  body += '\n';  break;
                    case 't':  body += '\t';  break;
                    case '\\': body += '\\';  break;
                    case '"':  body += '"';   break;

                    default:
                        throw std::runtime_error(
                            std::string( "\\") + text[ i] + " is not one of the escapes a payload is written with (\\r \\n \\t \\\\ \\\")");
                }
            }

            return Bytes( body);
        }

        //
        // "trace( V, -0.001 s, 1e-06 s, [ 5.0 4.6 ])" and its file-backed twin.
        //
        [[nodiscard]]
        auto parseTrace( const std::string_view text, const std::filesystem::path & baseDirectory) -> Waveform;

        [[nodiscard]]
        auto parseValue( const std::string_view text, const std::filesystem::path & baseDirectory) -> StimulusValue
        {
            if( text.empty())
            {
                throw std::runtime_error( "a value is missing");
            }

            if( text == "true")  { return true;  }
            if( text == "false") { return false; }

            if( text.starts_with( "trace("))
            {
                return parseTrace( text, baseDirectory);
            }

            if( text.front() == '"')
            {
                if( text.size() < 2 || text.back() != '"')
                {
                    throw std::runtime_error( "'" + std::string( text) + "' is missing its closing quote");
                }

                return parseTextPayload( text);
            }

            if( text.front() == '<')
            {
                if( text.back() != '>')
                {
                    throw std::runtime_error( "'" + std::string( text) + "' is missing its closing '>'");
                }

                //
                // Bytes::fromHex tolerates the spacing describeValue writes, so
                // <1B 5B 41> and <1B5B41> both work and a value can be pasted
                // straight out of a log.
                //
                try
                {
                    return Bytes::fromHex( text.substr( 1, text.size() - 2));
                }
                catch( const std::invalid_argument & error)
                {
                    throw std::runtime_error( "'" + std::string( text) + "' is not hex -- " + error.what());
                }
            }

            return parseQuantity( text);
        }

        //
        // The numbers in a trace's sample file: whitespace-separated, one per
        // line or not, so that whatever produced it -- a scope export, a
        // spreadsheet column, a model -- needs no reformatting.
        //
        [[nodiscard]]
        auto readSampleFile( const std::filesystem::path & path) -> std::vector<double>
        {
            std::ifstream in( path);

            if( !in)
            {
                throw std::runtime_error( "could not open the sample file '" + path.string() + "'");
            }

            std::vector<double>  samples;
            std::string          token;

            while( in >> token)
            {
                samples.push_back( parseNumber( token, "a sample"));
            }

            return samples;
        }

        auto parseTrace( const std::string_view text, const std::filesystem::path & baseDirectory) -> Waveform
        {
            if( text.back() != ')')
            {
                throw std::runtime_error( "'" + std::string( text) + "' is missing its closing ')'");
            }

            const auto inside    = text.substr( std::string_view( "trace(").size(), text.size() - std::string_view( "trace(").size() - 1);
            const auto arguments = splitValues( inside);

            if( arguments.size() != 4)
            {
                throw std::runtime_error(
                    "a trace takes four arguments -- trace( unit, origin, increment, samples) -- and '" +
                    std::string( text) + "' has " + std::to_string( arguments.size()));
            }

            const auto kind = quantityKindFromSymbol( arguments[ 0]);

            if( !kind)
            {
                throw std::runtime_error( "'" + std::string( arguments[ 0]) + "' is not a unit symbol");
            }

            //
            // Through the quantity parser, so a trace's timebase is written the
            // way every other reading in the file is and is checked against the
            // same symbol table. Both have to be times: a trace whose samples
            // were a volt apart is not a thing.
            //
            const auto origin    = parseQuantity( arguments[ 1]);
            const auto increment = parseQuantity( arguments[ 2]);

            if( static_cast<QuantityKind>( origin.index())    != quantityKindOf<quantities::Time>() ||
                static_cast<QuantityKind>( increment.index()) != quantityKindOf<quantities::Time>())
            {
                throw std::runtime_error( "a trace's origin and increment are times, written with 's'");
            }

            const auto samplesText = arguments[ 3];

            auto samples = std::vector<double>{};

            if( samplesText.starts_with( "\""))
            {
                if( samplesText.size() < 2 || samplesText.back() != '"')
                {
                    throw std::runtime_error( "'" + std::string( samplesText) + "' is missing its closing quote");
                }

                samples = readSampleFile( baseDirectory / samplesText.substr( 1, samplesText.size() - 2));
            }
            else if( samplesText.starts_with( "["))
            {
                if( samplesText.back() != ']')
                {
                    throw std::runtime_error( "'" + std::string( samplesText) + "' is missing its closing ']'");
                }

                std::istringstream  numbers{ std::string( samplesText.substr( 1, samplesText.size() - 2)) };
                std::string         token;

                while( numbers >> token)
                {
                    samples.push_back( parseNumber( token, "a sample"));
                }
            }
            else
            {
                throw std::runtime_error(
                    "a trace's samples are either [ 5.0 4.6 ] or \"a-file\", and '" +
                    std::string( samplesText) + "' is neither");
            }

            return Waveform{
                *kind,
                Waveform::Timing{ asQuantity<quantities::Time>( origin), asQuantity<quantities::Time>( increment) },
                std::move( samples) };
        }

        //
        // Programs one key from its parsed values -- the seam chosen by which
        // alternative they hold, and sticky-versus-sequence by how many there
        // are. See core/stimulus.hpp on why one value is not a one-element list.
        //
        template<typename T, typename InjectOne, typename InjectMany>
        auto programAs(
            const std::vector<StimulusValue> &  values,
            const InjectOne &                   one,
            const InjectMany &                  many) -> void
        {
            if( values.size() == 1)
            {
                one( std::get<T>( values.front()));

                return;
            }

            std::vector<T> sequence;

            sequence.reserve( values.size());

            for( const auto & value : values)
            {
                sequence.push_back( std::get<T>( value));
            }

            many( std::move( sequence));
        }

        auto program( SessionBank & bank, const std::string & key, const std::vector<StimulusValue> & values) -> void
        {
            //
            // All four alternatives have to agree, and the first one decides.
            // A key answers one kind of question -- a point that is a voltage
            // on one read and a serial reply on the next is a typo, and
            // programming it on two seams would leave the second one silently
            // unreachable.
            //
            const auto shape = values.front().index();

            for( const auto & value : values)
            {
                if( value.index() != shape)
                {
                    throw std::runtime_error( "'" + key + "' mixes readings, flags, payloads or traces in one list");
                }
            }

            switch( shape)
            {
                case 0:
                    programAs<QuantityVariant>( values,
                        [&]( QuantityVariant v)              { bank.inject( key, std::move( v)); },
                        [&]( std::vector<QuantityVariant> v) { bank.inject( key, sourceOf( std::move( v))); });
                    break;

                case 1:
                    programAs<Bytes>( values,
                        [&]( Bytes v)              { bank.injectData( key, std::move( v)); },
                        [&]( std::vector<Bytes> v) { bank.injectData( key, dataSourceOf( std::move( v))); });
                    break;

                case 2:
                    programAs<bool>( values,
                        [&]( const bool v)        { bank.injectFlag( key, v); },
                        [&]( std::vector<bool> v) { bank.injectFlag( key, flagSourceOf( std::move( v))); });
                    break;

                default:
                    programAs<Waveform>( values,
                        [&]( Waveform v)              { bank.injectTrace( key, std::move( v)); },
                        [&]( std::vector<Waveform> v) { bank.injectTrace( key, traceSourceOf( std::move( v))); });
                    break;
            }
        }
    } // namespace

    auto unitSymbol( const QuantityKind kind) -> std::string_view
    {
        //
        // Asked of a value of that kind rather than looked up in a table --
        // Quantity<Unit>::symbol() is where a unit's spelling is written down
        // (see core/quantity.hpp), and anything else here would be a second
        // list to keep in step with it.
        //
        return std::visit( []( const auto & quantity) { return quantity.symbol(); },
                           quantityVariantFromKind( kind, 0.0));
    }

    auto quantityKindFromSymbol( const std::string_view symbol) -> std::optional<QuantityKind>
    {
        for( const auto kind : meta::values<QuantityKind>)
        {
            if( unitSymbol( kind) == symbol)
            {
                return kind;
            }
        }

        return std::nullopt;
    }

    auto injectStimulus(
        SessionBank &                  bank,
        std::istream &                 in,
        const std::filesystem::path &  baseDirectory) -> void
    {
        //
        // Parsed in full before anything is programmed, so a typo on the last
        // line leaves the bank untouched rather than half armed. A run started
        // from half a stimulus file fails somewhere unrelated to the mistake --
        // "nothing programmed for 'Osc1.Acquisition'", three scripts later --
        // and that is the diagnosis this ordering exists to prevent.
        //
        std::vector<std::pair<std::string, std::vector<StimulusValue>>> parsed;

        std::string  line;
        std::size_t  number = 0;

        while( std::getline( in, line))
        {
            ++number;

            const auto content = trimmed( withoutComment( line));

            if( content.empty())
            {
                continue;
            }

            //
            // Every failure below is re-thrown with the line and its number
            // attached. The parsers themselves say what is wrong with a token
            // and know nothing about where it came from, which is the split
            // that keeps their messages about the value rather than about the
            // file.
            //
            try
            {
                const auto separator = content.find( '=');

                if( separator == std::string_view::npos)
                {
                    throw std::runtime_error( "a stimulus line is 'key = value'");
                }

                const auto key = trimmed( content.substr( 0, separator));

                if( key.empty())
                {
                    throw std::runtime_error( "the key is missing");
                }

                if( key.find_first_of( " \t") != std::string_view::npos)
                {
                    //
                    // Rejected rather than accepted with the space in it. A key
                    // is a session key, none of which contain whitespace, so
                    // "Output 5V = 5 V" is a typo -- and accepting it would
                    // arm a slot nothing ever asks for, which surfaces later as
                    // "nothing programmed for 'Output5V'" pointing at the one
                    // line that was trying to program it.
                    //
                    throw std::runtime_error( "'" + std::string( key) + "' is not a session key -- keys have no spaces in them");
                }

                std::vector<StimulusValue> values;

                for( const auto & text : splitValues( content.substr( separator + 1)))
                {
                    values.push_back( parseValue( text, baseDirectory));
                }

                parsed.emplace_back( std::string( key), std::move( values));
            }
            catch( const std::exception & error)
            {
                throw std::runtime_error(
                    "stimulus line " + std::to_string( number) + ": " + error.what() + " -- in '" + std::string( trimmed( line)) + "'");
            }
        }

        for( auto & [ key, values] : parsed)
        {
            program( bank, key, values);
        }
    }

    auto injectStimulusFromFile( SessionBank & bank, const std::filesystem::path & path) -> void
    {
        std::ifstream in( path);

        if( !in)
        {
            throw std::runtime_error( "could not open the stimulus file '" + path.string() + "'");
        }

        //
        // The file's own directory, so a trace's samples sit beside the file
        // that names them and the pair can be moved together -- the same
        // pairing core::sidecarDirectoryFor gives a recording.
        //
        injectStimulus( bank, in, path.parent_path());
    }
} // namespace core
