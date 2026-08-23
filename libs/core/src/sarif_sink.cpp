#include "core/sarif_sink.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <ostream>
#include <stdexcept>

#include "core/format.hpp"

namespace core
{
    namespace
    {
        constexpr std::string_view kSarifSchema  = "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json";
        constexpr std::string_view kSarifVersion = "2.1.0";
        constexpr std::string_view kToolUri      = "https://github.com/thomasrambrant/thorium";

        //
        // The one rule every ad-hoc check reports under -- see ruleIdFor below
        // for why they are not indexed by a criterion. Spelled like the other
        // verb rules ("Thorium/Measure", "Thorium/Connect") because that is
        // what it is: the Verify verb's own rule, for the results that have no
        // criterion behind them. A CRITERIA table named "Thorium" holding a
        // CRIT named "Verify" would collide with it, which is not a case worth
        // defending against.
        //
        constexpr std::string_view kAdHocVerifyRule = "Thorium/Verify";

        //
        // Indentation, as whole strings rather than a computed repeat: the
        // document's nesting is fixed and shallow, and naming the levels makes
        // the writer below read like the JSON it produces.
        //
        constexpr std::string_view kI1 = "  ";
        constexpr std::string_view kI2 = "    ";
        constexpr std::string_view kI3 = "      ";
        constexpr std::string_view kI4 = "        ";
        constexpr std::string_view kI5 = "          ";
        constexpr std::string_view kI6 = "            ";

        //
        // A quoted, escaped JSON string. Free function taking the sink's own
        // escape() so every string written goes through exactly one path --
        // there is no "this one can't contain a quote" shortcut anywhere below.
        //
        auto quoted( const std::string_view text) -> std::string
        {
            return "\"" + SarifSink::escape( text) + "\"";
        }

        //
        // One "key": "value" property, or nothing at all when the value is
        // empty. Omitting rather than writing "" matters for the same reason
        // core::report.cpp omits an unset metadata row: a consumer must be able
        // to tell "no serial number was supplied" from "the serial number is
        // the empty string".
        //
        auto optionalProperty( std::ostream & out, const std::string_view indent, const std::string_view key, const std::string_view value, bool & first) -> void
        {
            if( value.empty())
            {
                return;
            }

            if( !first)
            {
                out << ",\n";
            }

            out << indent << quoted( key) << ": " << quoted( value);
            first = false;
        }

        auto property( std::ostream & out, const std::string_view indent, const std::string_view key, const std::string_view value, bool & first) -> void
        {
            if( !first)
            {
                out << ",\n";
            }

            out << indent << quoted( key) << ": " << quoted( value);
            first = false;
        }

        //
        // The same, as a JSON boolean rather than a quoted string -- so a
        // consumer filtering on it writes `benchAttached == false` and not a
        // string comparison against whichever spelling this file chose.
        //
        // A differently named function rather than an overload of property()
        // above, and that is a trap avoided rather than a style choice: a
        // const char* converts to bool by a standard conversion and to
        // string_view by a user-defined one, so an overload set containing both
        // would silently route every string literal to the boolean version.
        //
        auto booleanProperty( std::ostream & out, const std::string_view indent, const std::string_view key, const bool value, bool & first) -> void
        {
            if( !first)
            {
                out << ",\n";
            }

            out << indent << quoted( key) << ": " << ( value ? "true" : "false");
            first = false;
        }

        //
        // SARIF's `level` (how bad) and `kind` (what sort of finding) are two
        // separate axes, and a test log needs both: a passing check is kind
        // "pass" at level "none", a failing one is kind "fail" at level "error",
        // and a Measure or a Connect is kind "informational" -- it is not a
        // finding at all, it is what the tool did on the way to one. Getting
        // this wrong is what makes a SARIF consumer report a run's every
        // measurement as an issue.
        //
        auto levelFor( const JournalEvent & event) -> std::string_view
        {
            if( event.Method == Verb::Verify)
            {
                return event.Passed.value_or( false) ? "none" : "error";
            }

            return "none";
        }

        auto kindFor( const JournalEvent & event) -> std::string_view
        {
            if( event.Method == Verb::Verify)
            {
                return event.Passed.value_or( false) ? "pass" : "fail";
            }

            return "informational";
        }

        //
        // What this result is *about*, for the machine log's logical location.
        //
        // Usually the subject: a criterion id, a DUT point, an instrument. An
        // ad-hoc check has none of those -- core/verify.hpp's three-argument
        // Verify puts its one line of prose in the description and leaves the
        // subject empty, because the human report's subject column is sized for
        // an identifier and a sentence there wrecks the row. This file has no
        // columns to wreck, so it uses the prose: a logical location with no
        // name at all would leave a consumer unable to say which of a run's
        // ad-hoc checks it was looking at, and the prose is the only thing that
        // distinguishes them.
        //
        // Two sinks rendering one record differently, deliberately -- the
        // constraint that shapes the human row does not exist here.
        //
        auto subjectName( const JournalEvent & event) -> std::string
        {
            return event.Subject.empty() ? event.Detail : event.Subject;
        }

        //
        // The result's own message -- the one field a consumer displays without
        // knowing anything about Thorium, so it has to stand alone: what was
        // done, to what, with what result.
        //
        auto messageFor( const JournalEvent & event) -> std::string
        {
            std::string message( to_string( event.Method));

            const auto subject = subjectName( event);

            if( !subject.empty())
            {
                message += " " + subject;
            }

            if( !event.Value.empty())
            {
                message += " = " + event.Value;
            }

            if( !event.Instrument.empty() && event.Instrument != subject)
            {
                message += " via " + event.Instrument;
            }

            if( event.Passed.has_value())
            {
                message += event.Passed.value() ? " [PASS]" : " [FAIL]";
            }

            //
            // Skipped when the description is already standing in as the
            // subject -- an ad-hoc check has one line of prose, and a message
            // reading "Verify X = 5 V [PASS] -- X" says it twice.
            //
            if( !event.Detail.empty() && event.Detail != subject)
            {
                message += " -- " + event.Detail;
            }

            return message;
        }

        auto fullyQualifiedName( const JournalEvent & event) -> std::string
        {
            std::string name;

            if( !event.Group.empty())
            {
                name += event.Group;
            }

            if( !event.Test.empty())
            {
                name += ( name.empty() ? "" : "/") + event.Test;
            }

            if( const auto subject = subjectName( event); !subject.empty())
            {
                name += ( name.empty() ? "" : "/") + subject;
            }

            return name;
        }
    } // namespace

    auto SarifSink::escape( const std::string_view text) -> std::string
    {
        std::string result;
        result.reserve( text.size() + text.size() / 8);

        for( const char c : text)
        {
            switch( c)
            {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b";  break;
                case '\f': result += "\\f";  break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;

                default:
                    if( static_cast<unsigned char>( c) < 0x20)
                    {
                        //
                        // JSON requires every remaining control byte to be
                        // escaped as \u00XX -- unlike the RTF sink, which can
                        // drop them, a raw control byte here makes the document
                        // invalid rather than merely ugly.
                        //
                        std::array<char, 8> escaped{};
                        std::snprintf( escaped.data(), escaped.size(), "\\u%04x", static_cast<unsigned>( static_cast<unsigned char>( c)));
                        result += escaped.data();
                        break;
                    }

                    //
                    // Bytes above ASCII are passed through untouched: JSON is
                    // UTF-8 by default, so valid UTF-8 input stays valid, and
                    // re-encoding it would only risk breaking multi-byte
                    // sequences this has no reason to decode.
                    //
                    result += c;
                    break;
            }
        }

        return result;
    }

    auto SarifSink::ruleIdFor( const JournalEvent & event) -> std::string
    {
        if( event.Method == Verb::Verify)
        {
            //
            // A table-declared criterion IS a SARIF rule, in the format's own
            // sense of the word: a stable id with prose attached, evaluated
            // repeatedly, one result per evaluation. So its CRITERIA group and
            // id -- the same two strings a CRIT entry declares (see
            // core/criterion.hpp), and the pair a test spec traces to -- become
            // the ruleId, and a consumer's "results for rule X" query means
            // "every time this requirement was checked" across runs, tests and
            // DUTs. Deliberately SubjectGroup, not the running test's Group: a
            // criterion checked from two different catalog tests is one rule
            // with two results.
            //
            // An ad-hoc check is not that, and is deliberately not pretended to
            // be. It is an assertion written inline in one script, with no
            // entry in any table and no identity to track between runs -- so
            // rather than indexing it by a name someone had to invent, every
            // ad-hoc result shares this one rule. Its prose is still in the
            // file, and in the places a reader and a query actually want it:
            // logicalLocations[].name, the fullyQualifiedName that qualifies it
            // by group and test, and the message. What is gone is the pretence
            // that it was a stable key. Promoting the check to a CRIT is what
            // buys back per-criterion grouping -- see core/verify.hpp's
            // three-argument overload.
            //
            // An empty SubjectGroup is what distinguishes the two, and is not a
            // heuristic: it is precisely what that overload constructs.
            //
            return event.SubjectGroup.empty() ? std::string( kAdHocVerifyRule)
                                              : event.SubjectGroup + "/" + event.Subject;
        }

        return "Thorium/" + std::string( to_string( event.Method));
    }

    SarifSink::SarifSink( std::string path) :
        mPath( std::move( path))
    {}

    SarifSink::~SarifSink()
    {
        //
        // Best-effort: a destructor must not throw, and a log that cannot be
        // written during stack unwinding is not worth terminating over -- see
        // this class's own comment on why the attempt is made at all.
        //
        try
        {
            if( !mWritten)
            {
                writeLog( false);
            }
        }
        catch( ... )
        {
        }
    }

    auto SarifSink::onRunStart( const RunInfo & info) -> void
    {
        mRunInfo = info;
    }

    auto SarifSink::noteRule( const JournalEvent & event) -> void
    {
        const auto id = ruleIdFor( event);

        const auto existing = std::find_if( mRules.begin(), mRules.end(),
            [&id]( const auto & rule) { return rule.first == id; });

        if( existing != mRules.end())
        {
            //
            // A verb rule ("Thorium/Measure") is registered without a
            // description the first time it is seen and never gains one; a
            // criterion rule keeps the first description it was given. Both are
            // stable across a run by construction -- a CRIT's description is a
            // compile-time constant.
            //
            return;
        }

        //
        // A criterion rule is described by the criterion's own prose. The
        // shared ad-hoc rule cannot be -- it stands for every inline check in
        // the run, which have nothing in common to describe -- so it gets a
        // fixed sentence saying what kind of result belongs to it, and each
        // result carries its own text in its message and logical location.
        //
        if( event.Method == Verb::Verify)
        {
            mRules.emplace_back( id, ( id == kAdHocVerifyRule)
                ? std::string( "An inline check in a test script, with no criteria-table entry of its own")
                : event.Detail);

            return;
        }

        mRules.emplace_back( id, "Thorium " + std::string( to_string( event.Method)) + " step");
    }

    auto SarifSink::onEvent( const JournalEvent & event) -> void
    {
        noteRule( event);
        mEvents.push_back( event);
    }

    auto SarifSink::onRunEnd( const bool allPassed) -> void
    {
        writeLog( allPassed);
    }

    auto SarifSink::writeLog( const bool allPassed) -> void
    {
        //
        // Set before writing, not after: if the write throws, the destructor
        // must not try again and throw from there.
        //
        mWritten = true;

        std::ofstream out( mPath, std::ios::out | std::ios::trunc);

        if( !out)
        {
            throw std::runtime_error( "SarifSink: could not open '" + mPath + "' for writing");
        }

        const auto endedUtc = isoUtcFromUnixMillis( unixMillisNow());

        out << "{\n"
            << kI1 << quoted( "$schema") << ": " << quoted( kSarifSchema) << ",\n"
            << kI1 << quoted( "version") << ": " << quoted( kSarifVersion) << ",\n"
            << kI1 << quoted( "runs") << ": [\n"
            << kI2 << "{\n";

        // --- tool.driver: which framework produced this, exactly ---
        out << kI3 << quoted( "tool") << ": {\n"
            << kI4 << quoted( "driver") << ": {\n"
            << kI5 << quoted( "name") << ": " << quoted( mRunInfo.FrameworkName) << ",\n"
            << kI5 << quoted( "version") << ": " << quoted( mRunInfo.FrameworkVersion) << ",\n"
            << kI5 << quoted( "semanticVersion") << ": " << quoted( mRunInfo.FrameworkVersion) << ",\n"
            << kI5 << quoted( "informationUri") << ": " << quoted( kToolUri) << ",\n"
            << kI5 << quoted( "rules") << ": [\n";

        for( std::size_t i = 0; i < mRules.size(); ++i)
        {
            const auto & [ id, description ] = mRules[ i];

            out << kI6 << "{\n"
                << kI6 << kI1 << quoted( "id") << ": " << quoted( id);

            if( !description.empty())
            {
                out << ",\n" << kI6 << kI1 << quoted( "shortDescription") << ": { "
                    << quoted( "text") << ": " << quoted( description) << " }";
            }

            out << "\n" << kI6 << "}" << ( ( i + 1 < mRules.size()) ? ",\n" : "\n");
        }

        out << kI5 << "]\n"
            << kI4 << "}\n"
            << kI3 << "},\n";

        //
        // --- invocations: the run itself ---
        // executionSuccessful is "did the tool manage to run", which is not the
        // same question as "did the DUT pass" -- a run that completed with
        // failing checks executed perfectly successfully. The DUT's verdict is
        // in the results, and restated in run.properties.allPassed below.
        //
        out << kI3 << quoted( "invocations") << ": [\n"
            << kI4 << "{\n"
            << kI5 << quoted( "executionSuccessful") << ": true,\n"
            << kI5 << quoted( "startTimeUtc") << ": " << quoted( mRunInfo.StartedUtc) << ",\n"
            << kI5 << quoted( "endTimeUtc") << ": " << quoted( endedUtc);

        {
            bool first = false;   // the properties above are already written

            optionalProperty( out, kI5, "commandLine", mRunInfo.CommandLine, first);
            optionalProperty( out, kI5, "machine",     mRunInfo.HostName,    first);
            optionalProperty( out, kI5, "account",     mRunInfo.Operator,    first);
        }

        out << "\n"
            << kI4 << "}\n"
            << kI3 << "],\n";

        //
        // --- automationDetails: what makes two runs comparable ---
        // The id is deliberately built from DUT + criteria variant + start
        // time: a consumer grouping runs sees "this DUT, against this tolerance
        // table" as one series, which is exactly the comparison a bench cares
        // about, and the timestamp keeps individual runs distinct within it.
        //
        out << kI3 << quoted( "automationDetails") << ": {\n"
            << kI4 << quoted( "id") << ": "
            << quoted( "thorium/" + mRunInfo.DutName + "/" + mRunInfo.CriteriaVariant + "/" + mRunInfo.StartedUtc) << ",\n"
            << kI4 << quoted( "description") << ": { " << quoted( "text") << ": "
            << quoted( mRunInfo.FrameworkName + " " + mRunInfo.FrameworkVersion + " run against " + mRunInfo.DutName
                       + " (criteria variant: " + mRunInfo.CriteriaVariant + ")") << " }\n"
            << kI3 << "},\n";

        //
        // --- run.properties: the full traceability bag ---
        // Every RunInfo field again, individually addressable. The invocation
        // and automationDetails blocks above are where a *generic* SARIF
        // consumer looks; this is where a Thorium-aware one reads the facts
        // SARIF has no standard slot for (which DUT, which serial, which rig,
        // which tolerance table).
        //
        out << kI3 << quoted( "properties") << ": {\n";

        {
            bool first = true;

            property(         out, kI4, "frameworkName",    mRunInfo.FrameworkName,    first);
            property(         out, kI4, "frameworkVersion", mRunInfo.FrameworkVersion, first);
            property(         out, kI4, "criteriaVariant",  mRunInfo.CriteriaVariant,  first);
            property(         out, kI4, "dutName",          mRunInfo.DutName,          first);
            optionalProperty( out, kI4, "dutSerial",        mRunInfo.DutSerial,        first);
            property(         out, kI4, "rigName",          mRunInfo.RigName,          first);
            optionalProperty( out, kI4, "operator",         mRunInfo.Operator,         first);
            optionalProperty( out, kI4, "hostName",         mRunInfo.HostName,         first);
            optionalProperty( out, kI4, "commandLine",      mRunInfo.CommandLine,      first);

            //
            // Not optional, unlike its neighbours: those are empty when nobody
            // supplied them, and "no answer" is a meaningful state for an
            // operator name. This is a yes/no the framework always knows, and a
            // consumer filtering out the runs that never touched hardware needs
            // it present on every run rather than only on the ones where it
            // happens to be interesting.
            //
            booleanProperty(  out, kI4, "benchAttached",    mRunInfo.BenchAttached,    first);

            //
            // Which revision of the tested content produced this -- see
            // RunInfo::SuiteVersion. Three separate properties even where they
            // currently agree: a consumer correlating runs needs to be able to
            // ask "same rig wiring, different criteria?" without splitting a
            // combined string back apart.
            //
            optionalProperty( out, kI4, "suiteVersion",     mRunInfo.SuiteVersion,     first);
            optionalProperty( out, kI4, "dutVersion",       mRunInfo.DutVersion,       first);
            optionalProperty( out, kI4, "rigVersion",       mRunInfo.RigVersion,       first);

            property(         out, kI4, "startedUtc",       mRunInfo.StartedUtc,       first);
            optionalProperty( out, kI4, "startedLocal",     mRunInfo.StartedLocal,     first);
            property(         out, kI4, "endedUtc",         endedUtc,                  first);

            out << ",\n" << kI4 << quoted( "allPassed") << ": " << ( allPassed ? "true" : "false") << "\n";
        }

        out << kI3 << "},\n";

        // --- results: every verb, in order ---
        out << kI3 << quoted( "results") << ": [\n";

        for( std::size_t i = 0; i < mEvents.size(); ++i)
        {
            const auto & event = mEvents[ i];

            out << kI4 << "{\n"
                << kI5 << quoted( "ruleId") << ": " << quoted( ruleIdFor( event)) << ",\n"
                << kI5 << quoted( "level") << ": " << quoted( levelFor( event)) << ",\n"
                << kI5 << quoted( "kind") << ": " << quoted( kindFor( event)) << ",\n"
                << kI5 << quoted( "message") << ": { " << quoted( "text") << ": " << quoted( messageFor( event)) << " },\n";

            //
            // logicalLocations rather than physicalLocation: SARIF's usual
            // "where" is a file and a line, and a measurement has neither. A
            // logical location is the format's own answer for a result that
            // belongs to a named thing instead of a place in a file -- here,
            // the DUT point or criterion, qualified by the test that reached
            // it.
            //
            out << kI5 << quoted( "locations") << ": [\n"
                << kI6 << "{\n"
                << kI6 << kI1 << quoted( "logicalLocations") << ": [\n"
                << kI6 << kI2 << "{\n"
                << kI6 << kI2 << kI1 << quoted( "name") << ": " << quoted( subjectName( event)) << ",\n"
                << kI6 << kI2 << kI1 << quoted( "fullyQualifiedName") << ": " << quoted( fullyQualifiedName( event)) << ",\n"
                << kI6 << kI2 << kI1 << quoted( "kind") << ": " << quoted( "member") << "\n"
                << kI6 << kI2 << "}\n"
                << kI6 << kI1 << "]\n"
                << kI6 << "}\n"
                << kI5 << "],\n";

            out << kI5 << quoted( "properties") << ": {\n";

            {
                bool first = true;

                property(         out, kI6, "verb",          to_string( event.Method), first);
                optionalProperty( out, kI6, "group",         event.Group,              first);
                optionalProperty( out, kI6, "test",          event.Test,               first);
                optionalProperty( out, kI6, "criteriaGroup", event.SubjectGroup,       first);
                optionalProperty( out, kI6, "instrument",    event.Instrument,         first);
                optionalProperty( out, kI6, "value",         event.Value,              first);
                optionalProperty( out, kI6, "unit",          event.Unit,               first);

                //
                // The tolerance the run actually enforced, as text -- so a
                // consumer reading a failure can report what was required
                // without resolving the ruleId back to a criteria file it may
                // not have. Deliberately alongside the rule's shortDescription
                // rather than instead of it: that is the criterion's prose, this
                // is its predicate, and they are different claims.
                //
                optionalProperty( out, kI6, "criterion",     event.CriterionText,      first);
                optionalProperty( out, kI6, "timeUtc",       event.TimeUtc,            first);

                //
                // The bare number alongside the formatted value, where there is
                // one -- so a consumer comparing a reading against a limit, or
                // trending it across runs, never has to parse "5.021 V" back
                // apart. formatNumber() rather than the stream's default
                // double formatting, so the two spellings of the same value in
                // this file agree digit for digit.
                //
                if( event.Numeric.has_value())
                {
                    out << ",\n" << kI6 << quoted( "numericValue") << ": " << formatNumber( event.Numeric.value());
                }

                out << ",\n" << kI6 << quoted( "sequence") << ": " << event.Sequence << "\n";
            }

            out << kI5 << "}\n"
                << kI4 << "}" << ( ( i + 1 < mEvents.size()) ? ",\n" : "\n");
        }

        out << kI3 << "]\n"
            << kI2 << "}\n"
            << kI1 << "]\n"
            << "}\n";

        out.flush();

        if( !out)
        {
            throw std::runtime_error( "SarifSink: failed while writing '" + mPath + "'");
        }
    }
} // namespace core
