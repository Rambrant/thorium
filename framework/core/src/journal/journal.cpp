#include "core/journal/journal.hpp"

#include "core/session/bench.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <utility>

#include "core/criteria/criteria_variants.hpp"
#include "core/meta.hpp"

//
// Build-stamped traceability, handed in as compile definitions by
// framework/core/CMakeLists.txt. Defaulted here rather than left to fail, so core
// still compiles for a consumer that pulls this library in without setting
// them -- with an obviously-unset value ("unknown") rather than a plausible
// one, since a log claiming the wrong DUT or the wrong tolerance table is
// worse than a log admitting it doesn't know.
//
#ifndef THORIUM_FRAMEWORK_VERSION
    #define THORIUM_FRAMEWORK_VERSION "unknown"
#endif

#ifndef THORIUM_DUT_NAME
    #define THORIUM_DUT_NAME "unknown"
#endif

#ifndef THORIUM_RIG_NAME
    #define THORIUM_RIG_NAME "unknown"
#endif

//
// Content revisions -- see RunInfo::SuiteVersion. Supplied by the top-level
// CMakeLists.txt, which derives them from the deployment's git revision; each
// is separately overridable, since suite/, dut/ and rig/ need not come from one
// repository.
//
#ifndef THORIUM_SUITE_VERSION
    #define THORIUM_SUITE_VERSION "unknown"
#endif

#ifndef THORIUM_DUT_VERSION
    #define THORIUM_DUT_VERSION "unknown"
#endif

#ifndef THORIUM_RIG_VERSION
    #define THORIUM_RIG_VERSION "unknown"
#endif

namespace core
{
    namespace
    {
        //
        // First non-empty of the named environment variables, or empty. Used
        // for operator/host, both of which are spelled differently per
        // platform and shell and neither of which is worth a platform header
        // (<unistd.h> gethostname, GetComputerName) for a log field a caller
        // can override anyway -- see RunInfo's own comment on the difference
        // between a baked-in fact and an observed convenience.
        //
        auto firstEnv( const std::initializer_list<const char *> names) -> std::string
        {
            for( const auto * name : names)
            {
                if( const char * value = std::getenv( name); value != nullptr && *value != '\0')
                {
                    return value;
                }
            }

            return {};
        }
    } // namespace

    //
    // Reflects over Verb's own enumerators (core::meta::to_string, see
    // core/meta.hpp) -- the enumerator's spelling is exactly the string both
    // logs want ("Measure", "Verify"), so there is nothing here to keep in
    // sync with the enum. Same fit as core::to_string(QuantityKind).
    //
    auto to_string( const Verb verb) -> std::string_view
    {
        return meta::to_string( verb);
    }

    auto unixMillisNow() -> std::int64_t
    {
        return static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    //
    // strftime rather than std::format's chrono support: the format string is
    // fixed, there is no locale involved, and this keeps the timestamp spelling
    // in one place small enough to read. std::gmtime's shared static buffer is
    // fine here -- a suite run is single-threaded by construction (one script
    // at a time, see framework/runner/src/main.cpp), and the result is copied out
    // immediately.
    //
    auto isoUtcFromUnixMillis( const std::int64_t millis) -> std::string
    {
        //
        // Floor division, not truncation: a negative epoch millisecond count
        // (a machine with its clock badly wrong, which is exactly when a
        // timestamp gets scrutinised) must not round its seconds *up* and
        // leave a negative millisecond remainder to print.
        //
        const auto seconds      = static_cast<std::time_t>( ( millis >= 0) ? millis / 1000 : ( millis - 999) / 1000);
        const auto milliseconds = static_cast<int>( millis - static_cast<std::int64_t>( seconds) * 1000);

        const std::tm * utc = std::gmtime( &seconds);

        if( utc == nullptr)
        {
            return "unknown";
        }

        std::array<char, 24> date{};
        std::strftime( date.data(), date.size(), "%Y-%m-%dT%H:%M:%S", utc);

        std::array<char, 32> stamp{};
        const auto written = std::snprintf( stamp.data(), stamp.size(), "%s.%03dZ", date.data(), milliseconds);

        return ( written > 0) ? std::string( stamp.data(), static_cast<std::size_t>( written)) : std::string{};
    }

    //
    // strftime again, with localtime rather than gmtime -- see
    // isoUtcFromUnixMillis above on why strftime and not std::format, and on
    // the shared-static-buffer question. No sub-second precision here on
    // purpose: this field is for a person placing the run in their day, and
    // milliseconds are noise for that. The UTC field keeps them.
    //
    auto localTimeFromUnixMillis( const std::int64_t millis) -> std::string
    {
        const auto seconds = static_cast<std::time_t>( ( millis >= 0) ? millis / 1000 : ( millis - 999) / 1000);

        const std::tm * local = std::localtime( &seconds);

        if( local == nullptr)
        {
            return "unknown";
        }

        std::array<char, 64> stamp{};

        // %Z is the zone abbreviation -- see this function's declaration on why
        // a local time without one is not much use once the log travels.
        const auto written = std::strftime( stamp.data(), stamp.size(), "%a %d %b %Y %H:%M:%S %Z", local);

        return ( written > 0) ? std::string( stamp.data(), written) : std::string{};
    }

    auto defaultRunInfo() -> RunInfo
    {
        //
        // One clock reading for both spellings -- see RunInfo's own comment on
        // why a report must not be able to disagree with itself about when it
        // started.
        //
        const auto startedAt = unixMillisNow();

        return RunInfo{
            .FrameworkName    = "Thorium",
            .FrameworkVersion = THORIUM_FRAMEWORK_VERSION,

            //
            // Read from the runtime selection rather than baked in at build
            // time: every known variant is compiled into the binary and a run
            // picks one with --criteria= (see core/criteria/criteria_variants.hpp), so a
            // build-time constant here would report the *default* on every run,
            // including the ones that overrode it. Journal::begin() freezes the
            // selection immediately after this is assembled, so what the header
            // claims and what the checks below it applied cannot come apart.
            //
            .CriteriaVariant  = std::string( activeCriteriaVariantName()),

            //
            // Build-stamped, and so the one criteria field here that a run
            // cannot influence -- which is the point of reporting it: it says
            // where the rows the applied variant did not change came from.
            //
            .CriteriaMaster   = std::string( masterCriteriaVariantName()),
            .DutName          = THORIUM_DUT_NAME,
            .DutSerial        = {},
            .RigName          = THORIUM_RIG_NAME,
            .Operator         = firstEnv( { "THORIUM_OPERATOR", "USER", "LOGNAME", "USERNAME" }),
            .HostName         = firstEnv( { "THORIUM_HOST", "HOSTNAME", "HOST", "COMPUTERNAME" }),
            .CommandLine      = {},
            .SuiteVersion     = THORIUM_SUITE_VERSION,
            .DutVersion       = THORIUM_DUT_VERSION,
            .RigVersion       = THORIUM_RIG_VERSION,
            .StartedUtc       = isoUtcFromUnixMillis( startedAt),
            .StartedLocal     = localTimeFromUnixMillis( startedAt),

            //
            // Read here rather than left to the caller, so that no caller can
            // forget it and produce a header claiming a bench that was not
            // there. Which does put an ordering requirement on whoever detaches:
            // do it before assembling this, not after. framework/runner/src/main.cpp
            // therefore detaches immediately after parsing its options, ahead
            // of everything else it does -- see the comment there, which says
            // so at the point where the order matters.
            //
            .BenchAttached    = bench().isAttached()
        };
    }

    auto Journal::add( IJournalSink & sink) -> void
    {
        mSinks.push_back( &sink);
    }

    auto Journal::clearSinks() -> void
    {
        mSinks.clear();
    }

    auto Journal::begin( RunInfo info) -> void
    {
        //
        // The run has started, so the criteria variant is now part of a record
        // that has been committed to: the header about to reach every sink
        // names it. Changing it after this point would produce a log whose
        // header and whose checks disagree, with nothing in the file saying so
        // -- see core/criteria/criteria_variants.hpp.
        //
        lockCriteriaVariant();

        mRunInfo      = std::move( info);
        mNextSequence = 0;
        mGroup.clear();
        mTest.clear();
        mPhase.clear();

        for( auto * sink : mSinks)
        {
            sink->onRunStart( mRunInfo);
        }
    }

    auto Journal::end( const bool allPassed) -> void
    {
        for( auto * sink : mSinks)
        {
            sink->onRunEnd( allPassed);
        }
    }

    auto Journal::beginGroup( const std::string_view group, const std::string_view description) -> void
    {
        mGroup = group;

        for( auto * sink : mSinks)
        {
            sink->onGroupStart( group, description);
        }
    }

    auto Journal::endGroup() -> void
    {
        for( auto * sink : mSinks)
        {
            sink->onGroupEnd( mGroup);
        }

        // Cleared after the sinks are told, same as endTest below.
        mGroup.clear();
    }

    auto Journal::beginTest( const std::string_view test, const std::string_view description) -> void
    {
        mTest = test;

        mTestOpen     = true;
        mTestChecks   = 0;
        mTestFailures = 0;

        for( auto * sink : mSinks)
        {
            sink->onTestStart( test, description);
        }
    }

    auto Journal::beginPhase( const std::string_view phase, const std::string_view title) -> void
    {
        mPhase = phase;

        for( auto * sink : mSinks)
        {
            sink->onPhaseStart( mGroup, phase, title);
        }
    }

    auto Journal::endPhase() -> void
    {
        for( auto * sink : mSinks)
        {
            sink->onPhaseEnd( mPhase);
        }

        // Cleared after the sinks are told, same as endTest below.
        mPhase.clear();
    }

    auto Journal::endTest() -> bool
    {
        //
        // A test that checked nothing says so, in the log, before its verdict
        // -- rather than closing on a bare RESULT [FAIL] with nothing above it,
        // which reads like a bug in the runner instead of a finding about the
        // script. Same argument core::Fail rests on, and the same placeholder
        // in the value column.
        //
        // Posted through post() like any other event, so it is sequenced,
        // stamped and attributed to this test exactly as a real check would be
        // -- and so it lands in the tally below, which is why the verdict is
        // computed after it rather than before.
        //
        if( mTestOpen && mTestChecks == 0)
        {
            post( JournalRecord{
                .Method = Verb::Verify,
                .Detail = "no check was recorded -- a test that verified nothing cannot pass",
                .Value  = std::string( kUncheckedValue),
                .Passed = false
            });
        }

        const bool passed = mTestChecks > 0 && mTestFailures == 0;

        mTestOpen = false;

        for( auto * sink : mSinks)
        {
            sink->onTestEnd( mGroup, mTest, passed);
        }

        //
        // Cleared after the sinks are told, not before: onTestEnd is handed
        // the group/test it is closing, and anything posted afterwards (a
        // Safe from a guard destructor, say) belongs to the run rather than
        // to the test that just finished. The group is left alone -- it is
        // still open until endGroup(), and a further test may follow.
        //
        mTest.clear();

        return passed;
    }

    auto Journal::post( JournalRecord record) -> void
    {
        //
        // The backstop on the value column -- see kMaxJournalValueLength in
        // core/journal/journal.hpp for why it is here and not at the call sites. Every
        // value this framework produces today is already well inside it
        // (core::describeValue abridges a payload with the byte count kept),
        // so this trims nothing that exists and stands in the way of the one
        // that does not exist yet.
        //
        if( record.Value.size() > kMaxJournalValueLength)
        {
            record.Value.resize( kMaxJournalValueLength - 3);
            record.Value += "...";
        }

        //
        // Sequence advances whether or not anyone is listening, so the numbers
        // in a log never depend on which sinks a run happened to have -- two
        // runs of the same script produce the same sequence for the same
        // event, which is what makes them diffable.
        //
        //
        // The running test's tally, kept here because this is the one place
        // every check passes through -- see endTest(), which is what reads it.
        // Keyed on Passed being set rather than on Verb::Verify: what makes an
        // event a check is that it carries a verdict, and an event that carries
        // one is exactly what the verdict of the test should turn on.
        //
        if( mTestOpen && record.Passed.has_value())
        {
            ++mTestChecks;

            if( ! *record.Passed)
            {
                ++mTestFailures;
            }
        }

        const auto millis = unixMillisNow();

        JournalEvent event{
            std::move( record),
            mNextSequence++,
            millis,
            isoUtcFromUnixMillis( millis),
            mGroup,
            mTest,
            mPhase
        };

        for( auto * sink : mSinks)
        {
            sink->onEvent( event);
        }
    }

    auto journal() -> Journal &
    {
        //
        // Function-local static, not a namespace-scope global: this is
        // constructed on first use, which is what keeps it from depending on
        // static-initialisation order against the verb engines that post to
        // it (Measure/Apply/Connect are themselves namespace-scope globals --
        // see hal/src/verbs/measure.cpp, hal/src/verbs/source.cpp and hal/src/verbs/route.cpp).
        //
        static Journal instance;

        return instance;
    }
} // namespace core
