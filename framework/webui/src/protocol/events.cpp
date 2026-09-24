#include "protocol/events.hpp"

#include "protocol/json.hpp"

namespace webui
{
    namespace
    {
        auto kindFrom( const std::string_view name) -> RunEvent::Kind
        {
            if( name == "runStart")   return RunEvent::Kind::RunStart;
            if( name == "groupStart") return RunEvent::Kind::GroupStart;
            if( name == "groupEnd")   return RunEvent::Kind::GroupEnd;
            if( name == "testStart")  return RunEvent::Kind::TestStart;
            if( name == "testEnd")    return RunEvent::Kind::TestEnd;
            if( name == "phaseStart") return RunEvent::Kind::PhaseStart;
            if( name == "phaseEnd")   return RunEvent::Kind::PhaseEnd;
            if( name == "event")      return RunEvent::Kind::Event;
            if( name == "runEnd")     return RunEvent::Kind::RunEnd;

            return RunEvent::Kind::Unknown;
        }

        //
        // An optional field, absent rather than defaulted. The schema omits a
        // key whose value would be empty or unset, so "the key is not there" is
        // the answer and not the absence of one -- see core::EventSink.
        //
        auto optionalBool( const Json & object, const std::string_view key) -> std::optional<bool>
        {
            const auto * field = object.at( key);

            if( !field || field->type() != Json::Type::Bool)
            {
                return std::nullopt;
            }

            return field->boolean();
        }

        auto optionalNumber( const Json & object, const std::string_view key) -> std::optional<double>
        {
            const auto * field = object.at( key);

            if( !field || field->type() != Json::Type::Number)
            {
                return std::nullopt;
            }

            return field->number();
        }

        auto readHeader( const Json & info) -> RunEvent::RunHeader
        {
            RunEvent::RunHeader header;

            header.DutName         = info.textAt( "dutName");
            header.DutSerial       = info.textAt( "dutSerial");
            header.RigName         = info.textAt( "rigName");
            header.CriteriaVariant = info.textAt( "criteriaVariant");
            header.CriteriaMaster  = info.textAt( "criteriaMaster");
            header.Operator        = info.textAt( "operator");
            header.StartedLocal    = info.textAt( "startedLocal");
            header.CommandLine     = info.textAt( "commandLine");

            //
            // Defaults to true when the key is missing, matching the field's
            // own default in core::RunInfo. The sink writes it
            // unconditionally, so a missing key means a runner older than the
            // field -- and an old runner is an attached one, since the detached
            // modes and this flag arrived together.
            //
            header.BenchAttached = info.boolAt( "benchAttached", true);

            if( const auto * instruments = info.at( "instruments"))
            {
                for( const auto & line : instruments->items())
                {
                    header.Instruments.push_back( line->text());
                }
            }

            return header;
        }
    } // namespace

    auto parseEvent( const std::string_view line) -> std::optional<RunEvent>
    {
        const auto document = Json::parse( line);

        if( !document || document->type() != Json::Type::Object)
        {
            return std::nullopt;
        }

        const auto * kind = document->at( "kind");

        if( !kind)
        {
            return std::nullopt;
        }

        RunEvent event;

        event.Which = kindFrom( kind->text());

        //
        // Read flat, without switching on the kind first.
        //
        // Every field is optional in the schema, so reading one a kind does not
        // carry costs a map lookup that answers nothing -- and a per-kind
        // reader would be nine blocks that each have to be kept in step with
        // core::EventSink by hand. The struct's own documentation says which
        // kinds carry what; enforcing that here would be a second copy of the
        // schema in the place least able to keep it honest.
        //
        event.Group         = document->textAt( "group");
        event.Test          = document->textAt( "test");
        event.Phase         = document->textAt( "phase");
        event.Description   = document->textAt( "description");
        event.Title         = document->textAt( "title");

        event.Verb          = document->textAt( "verb");
        event.Subject       = document->textAt( "subject");
        event.SubjectGroup  = document->textAt( "subjectGroup");
        event.Detail        = document->textAt( "detail");
        event.Instrument    = document->textAt( "instrument");
        event.Value         = document->textAt( "value");
        event.Unit          = document->textAt( "unit");
        event.CriterionText = document->textAt( "criterionText");
        event.Numeric       = optionalNumber( *document, "numeric");

        //
        // The verdict arrives under two spellings -- "passed" on a Verify and
        // on testEnd, "allPassed" on runEnd -- because the two are genuinely
        // different claims at the source and the schema does not flatten them.
        // One field here, because to this program they are the same question
        // asked of different scopes, and the scope is the kind.
        //
        event.Passed = event.Which == RunEvent::Kind::RunEnd
                            ? optionalBool( *document, "allPassed")
                            : optionalBool( *document, "passed");

        if( event.Which == RunEvent::Kind::RunStart)
        {
            if( const auto * info = document->at( "info"))
            {
                event.Header = readHeader( *info);
            }

            if( const auto * logs = document->at( "logs"))
            {
                event.SarifLog = logs->textAt( "sarif");
                event.RtfLog   = logs->textAt( "rtf");
            }
        }

        return event;
    }

    auto EventStream::consume( const std::string_view chunk) -> std::vector<RunEvent>
    {
        mPartial.append( chunk);

        std::vector<RunEvent> events;
        std::size_t           start = 0;

        for( ;;)
        {
            const auto newline = mPartial.find( '\n', start);

            if( newline == std::string::npos)
            {
                break;
            }

            auto line = std::string_view( mPartial).substr( start, newline - start);

            //
            // A trailing \r, for a stream that has been through a Windows pipe
            // in text mode. Stripped here rather than relied on not to happen:
            // the runner writes '\n' and the platform may or may not translate
            // it, and a reader that cared would be a reader that breaks on one
            // of the three platforms this program exists to run on.
            //
            if( !line.empty() && line.back() == '\r')
            {
                line.remove_suffix( 1);
            }

            //
            // A line that will not parse is dropped, not fatal. The likeliest
            // producer of one is a run killed mid-write, which is a thing this
            // program has to survive rather than report as a protocol error --
            // and the caller finds out the run died from the absent runEnd.
            //
            if( auto event = parseEvent( line))
            {
                events.push_back( std::move( *event));
            }

            start = newline + 1;
        }

        mPartial.erase( 0, start);

        return events;
    }
} // namespace webui
