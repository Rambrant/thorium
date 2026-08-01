#include "core/journal.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <utility>

#include "core/meta.hpp"

//
// Build-stamped traceability, handed in as compile definitions by
// libs/core/CMakeLists.txt. Defaulted here rather than left to fail, so core
// still compiles for a consumer that pulls this library in without setting
// them -- with an obviously-unset value ("unknown") rather than a plausible
// one, since a log claiming the wrong DUT or the wrong tolerance table is
// worse than a log admitting it doesn't know.
//
#ifndef THORIUM_FRAMEWORK_VERSION
    #define THORIUM_FRAMEWORK_VERSION "unknown"
#endif

#ifndef THORIUM_CRITERIA_VARIANT_NAME
    #define THORIUM_CRITERIA_VARIANT_NAME "unknown"
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
    // sync with the enum. Same fit as core::to_string(QuantityKind), and the
    // opposite of core::to_string(LogLevel), which wants shouting.
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
    // at a time, see app/src/main.cpp), and the result is copied out
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
            .CriteriaVariant  = THORIUM_CRITERIA_VARIANT_NAME,
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
            .StartedLocal     = localTimeFromUnixMillis( startedAt)
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
        mRunInfo      = std::move( info);
        mNextSequence = 0;
        mGroup.clear();
        mTest.clear();

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

        for( auto * sink : mSinks)
        {
            sink->onTestStart( test, description);
        }
    }

    auto Journal::endTest( const bool passed) -> void
    {
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
    }

    auto Journal::post( JournalRecord record) -> void
    {
        //
        // Sequence advances whether or not anyone is listening, so the numbers
        // in a log never depend on which sinks a run happened to have -- two
        // runs of the same script produce the same sequence for the same
        // event, which is what makes them diffable.
        //
        const auto millis = unixMillisNow();

        JournalEvent event{
            std::move( record),
            mNextSequence++,
            millis,
            isoUtcFromUnixMillis( millis),
            mGroup,
            mTest
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
        // see hal/src/measure.cpp and hal/src/apply.cpp).
        //
        static Journal instance;

        return instance;
    }
} // namespace core
