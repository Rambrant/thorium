//
// Ending a run early, end to end: the real runner and the real server, against a
// run that is genuinely in the middle of a test when the request arrives.
//
// test_run_session.cpp checks the plumbing against stand-in children and
// test_protocol.cpp checks that a stop *can* be seen; neither can say what a
// stop does to a run that is partway through a script, which is the whole claim
// -- that it ends the way a run ends: the test in progress finishes, both
// teardown hooks run, nothing further starts, and the verdict is "not passed".
//
// run_scripts_hooked is the subject (the real main.cpp over a two-test catalog),
// with THORIUM_FIXTURE_SCRIPT_DELAY_MS making its first script take long enough
// to be interrupted. It runs under --replay, which detaches the bench, so no
// instrument is needed -- and it is repeated 100000 times, so a run that was
// NOT stopped would be seen to be still going, not merely finished.
//
// Every wait here is bounded. A stop that does not work must fail a test, not
// hang the suite.
//
// POSIX only: it uses fork, pipes and signals.
//
#ifndef _WIN32

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "httplib.h"
#include "protocol/events.hpp"

namespace
{
    using Clock = std::chrono::steady_clock;

    const std::string kHooked = THORIUM_WEBUI_HOOKED_BINARY;
    const std::string kServer = THORIUM_WEBUI_SERVER_BINARY;

    // Long enough to be interrupted with room to spare, short enough that the
    // suite does not feel it.
    constexpr const char * kDelayMs = "1500";

    //
    // A child process with its stdin and stdout as pipes. stderr is captured
    // too, to a pipe nobody needs to drain: a run writes a line or two.
    //
    struct Child
    {
        pid_t        Pid{ -1 };
        int          In{ -1 };      // our write end of its stdin
        int          Out{ -1 };     // our read end of its stdout
        int          Err{ -1 };
        std::string  Partial;

        Child() = default;

        // Move-only: a copy would be a second owner, and an owner kills.
        Child( const Child &) = delete;
        auto operator=( const Child &) -> Child & = delete;

        Child( Child && other) noexcept { *this = std::move( other); }

        auto operator=( Child && other) noexcept -> Child &
        {
            if( this != &other)
            {
                release();
                Pid = std::exchange( other.Pid, -1);
                In  = std::exchange( other.In, -1);
                Out = std::exchange( other.Out, -1);
                Err = std::exchange( other.Err, -1);
                Partial = std::move( other.Partial);
            }
            return *this;
        }

        ~Child() { release(); }

        // A test that failed half way must not leave a run going.
        auto release() -> void
        {
            if( Pid > 0)
            {
                ::kill( Pid, SIGKILL);
                int status = 0;
                ::waitpid( Pid, &status, 0);
                Pid = -1;
            }
            for( int * fd : { &In, &Out, &Err})
            {
                if( *fd >= 0) { ::close( *fd); *fd = -1; }
            }
        }
    };

    auto spawn( const std::vector<std::string> & argv) -> Child
    {
        int in[ 2], out[ 2], err[ 2];
        EXPECT_EQ( ::pipe( in), 0);
        EXPECT_EQ( ::pipe( out), 0);
        EXPECT_EQ( ::pipe( err), 0);

        const auto pid = ::fork();

        if( pid == 0)
        {
            ::dup2( in[ 0],  STDIN_FILENO);
            ::dup2( out[ 1], STDOUT_FILENO);
            ::dup2( err[ 1], STDERR_FILENO);
            for( const int fd : { in[ 0], in[ 1], out[ 0], out[ 1], err[ 0], err[ 1]}) { ::close( fd); }

            std::vector<char *> raw;
            for( const auto & arg : argv) { raw.push_back( const_cast<char *>( arg.c_str())); }
            raw.push_back( nullptr);
            ::execv( raw.front(), raw.data());
            ::_exit( 127);
        }

        ::close( in[ 0]);
        ::close( out[ 1]);
        ::close( err[ 1]);

        Child child;
        child.Pid = pid;
        child.In  = in[ 1];
        child.Out = out[ 0];
        child.Err = err[ 0];

        return child;
    }

    //
    // Reads stdout until a complete line satisfies `done`, or the deadline, or
    // the pipe ends. Every line seen is appended to `lines`.
    //
    template<class Done>
    auto readLines( Child & child, std::vector<std::string> & lines, Done done, std::chrono::seconds limit) -> bool
    {
        const auto deadline = Clock::now() + limit;

        for( ;;)
        {
            for( auto pos = child.Partial.find( '\n'); pos != std::string::npos; pos = child.Partial.find( '\n'))
            {
                lines.push_back( child.Partial.substr( 0, pos));
                child.Partial.erase( 0, pos + 1);

                if( done( lines.back())) { return true; }
            }

            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>( deadline - Clock::now()).count();
            if( left <= 0) { return false; }

            pollfd p{ child.Out, POLLIN, 0};
            if( ::poll( &p, 1, static_cast<int>( left)) <= 0) { return false; }

            char buffer[ 4096];
            const auto n = ::read( child.Out, buffer, sizeof( buffer));
            if( n <= 0) { return false; }          // the run ended
            child.Partial.append( buffer, static_cast<std::size_t>( n));
        }
    }

    auto isTestStart( const std::string & line) -> bool
    {
        return line.find( R"("kind":"testStart")") != std::string::npos
            && line.find( R"("test":"FixtureTest")") != std::string::npos;
    }

    //
    // Waits for the child to end, bounded. Returns waitpid's status.
    //
    auto reap( Child & child, std::chrono::seconds limit) -> std::optional<int>
    {
        const auto deadline = Clock::now() + limit;

        while( Clock::now() < deadline)
        {
            int status = 0;
            if( ::waitpid( child.Pid, &status, WNOHANG) == child.Pid)
            {
                child.Pid = -1;
                return status;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 20));
        }

        return std::nullopt;
    }

    // What a finished run said, in the terms the tests assert on.
    struct Outcome
    {
        std::vector<std::string>  Lines;
        std::optional<bool>       AllPassed;      // unset: no runEnd
        std::vector<std::string>  Notes;          // journal Note details
        int                       Scripts{ 0 };   // "script" announcements
        int                       OtherScripts{ 0 };
        std::vector<std::string>  Teardowns;      // hook announcements, in order
        int                       TestEnds{ 0 };
        std::vector<bool>         Verdicts;       // each test's, in order
        std::vector<std::string>  DelayDetails;   // what each Delay event said
    };

    auto summarise( const std::vector<std::string> & lines) -> Outcome
    {
        Outcome outcome;
        outcome.Lines = lines;

        webui::EventStream reader;

        for( const auto & line : lines)
        {
            if( line.rfind( "HOOKFIXTURE ", 0) == 0)
            {
                const auto marker = line.substr( 12);
                if( marker == "script")       { ++outcome.Scripts; }
                if( marker == "other-script") { ++outcome.OtherScripts; }
                if( marker.find( "teardown") != std::string::npos) { outcome.Teardowns.push_back( marker); }
                continue;
            }

            for( const auto & event : reader.consume( line + "\n"))
            {
                using Kind = webui::RunEvent::Kind;

                if( event.Which == Kind::RunEnd)  { outcome.AllPassed = event.Passed; }
                if( event.Which == Kind::TestEnd)
                {
                    ++outcome.TestEnds;
                    outcome.Verdicts.push_back( event.Passed.value_or( false));
                }
                if( event.Which == Kind::Event && event.Verb == "Delay") { outcome.DelayDetails.push_back( event.Detail); }
                if( event.Which == Kind::Event && event.Verb == "Note") { outcome.Notes.push_back( event.Detail); }
            }
        }

        return outcome;
    }

    auto hasNote( const Outcome & outcome, const std::string & text) -> bool
    {
        return std::any_of( outcome.Notes.begin(), outcome.Notes.end(),
            [ &text]( const std::string & note) { return note.find( text) != std::string::npos; });
    }

    //
    // The fixture and the skeleton a replay of it needs. The fixture's scripts
    // read nothing, so the skeleton is empty -- what --replay is for here is
    // detaching the bench, not feeding readings.
    //
    class StopRun : public testing::Test
    {
        protected:
            auto SetUp() -> void override
            {
                Dir = std::filesystem::temp_directory_path() /
                      ( "thorium-stop-run-" + std::to_string( ::getpid()) + "-" +
                        testing::UnitTest::GetInstance()->current_test_info()->name());
                std::filesystem::remove_all( Dir);
                std::filesystem::create_directories( Dir);

                Skeleton = ( Dir / "readings.tsv").string();

                auto skeleton = spawn( { kHooked, "--quiet", "--no-logs", "--skeleton=" + Skeleton});
                std::vector<std::string> ignored;
                readLines( skeleton, ignored, []( const std::string &) { return false; }, std::chrono::seconds( 10));
                ASSERT_TRUE( reap( skeleton, std::chrono::seconds( 10)).has_value());

                ::setenv( "THORIUM_FIXTURE_SCRIPT_DELAY_MS", kDelayMs, 1);
            }

            auto TearDown() -> void override
            {
                ::unsetenv( "THORIUM_FIXTURE_SCRIPT_DELAY_MS");
                std::filesystem::remove_all( Dir);
            }

            // A run that would go on for hours if nothing stopped it, stopped
            // from outside while its first script is still sleeping.
            auto startLongRun( std::vector<std::string> extra = {}) -> Child
            {
                std::vector<std::string> argv{ kHooked, "--quiet", "--no-logs", "--events=-", "--stop-on-stdin",
                                               "--repeat=100000", "--replay=" + Skeleton};
                argv.insert( argv.end(), extra.begin(), extra.end());

                auto child = spawn( argv);
                std::vector<std::string> seen;

                EXPECT_TRUE( readLines( child, seen, isTestStart, std::chrono::seconds( 10))) << "the run never reached its script";
                Seen = seen;

                // Into the middle of the 1.5 s script, not its first instant.
                std::this_thread::sleep_for( std::chrono::milliseconds( 300));
                return child;
            }

            // Everything the run says from here until it ends, summarised.
            auto finish( Child & child) -> Outcome
            {
                std::vector<std::string> lines = Seen;
                readLines( child, lines, []( const std::string &) { return false; }, std::chrono::seconds( 20));
                return summarise( lines);
            }

            std::filesystem::path     Dir;
            std::string               Skeleton;
            std::vector<std::string>  Seen;
    };

    //
    // What every way of asking nicely must produce, so that the three tests
    // below assert one thing three ways rather than three similar things.
    //
    auto expectEndedTheWayARunEnds( const Outcome & outcome, const std::string & why) -> void
    {
        ASSERT_TRUE( outcome.AllPassed.has_value()) << "a stopped run still closes itself out with a runEnd";
        EXPECT_FALSE( *outcome.AllPassed) << "a stopped run is never a pass";
        EXPECT_TRUE( hasNote( outcome, why)) << "the log says why it ended";

        EXPECT_EQ( outcome.Scripts, 1) << "the test in progress finished, and no second pass started";
        EXPECT_EQ( outcome.TestEnds, 1);
        EXPECT_EQ( outcome.OtherScripts, 0) << "nothing further began";

        ASSERT_EQ( outcome.Teardowns.size(), 2u) << "both hooks ran: the group's, then the run's";
        EXPECT_EQ( outcome.Teardowns[ 0], "group-teardown");
        EXPECT_EQ( outcome.Teardowns[ 1], "teardown");
    }
}

// ---------------------------------------------------------------------------
// The runner, asked three ways
// ---------------------------------------------------------------------------

TEST_F( StopRun, AStopLineEndsARunMidTestTheWayARunEnds)
{
    auto child = startLongRun();

    ASSERT_EQ( ::write( child.In, "stop\n", 5), 5);

    const auto outcome = finish( child);
    const auto status  = reap( child, std::chrono::seconds( 10));

    expectEndedTheWayARunEnds( outcome, "stopped by operator");
    ASSERT_TRUE( status.has_value());
    EXPECT_TRUE( WIFEXITED( *status));
    EXPECT_EQ( WEXITSTATUS( *status), 1);
}

TEST_F( StopRun, CtrlCEndsARunMidTestTheWayARunEnds)
{
    auto child = startLongRun();

    ASSERT_EQ( ::kill( child.Pid, SIGINT), 0);

    const auto outcome = finish( child);
    const auto status  = reap( child, std::chrono::seconds( 10));

    expectEndedTheWayARunEnds( outcome, "stopped by operator");
    ASSERT_TRUE( status.has_value());
    EXPECT_TRUE( WIFEXITED( *status)) << "exited on its own, not killed by the signal";
    EXPECT_EQ( WEXITSTATUS( *status), 1);
}

TEST_F( StopRun, ASecondCtrlCEndsItAtOnceAndLeavesTheRigAsItWas)
{
    auto child = startLongRun();

    ASSERT_EQ( ::kill( child.Pid, SIGINT), 0);
    std::this_thread::sleep_for( std::chrono::milliseconds( 100));
    ASSERT_EQ( ::kill( child.Pid, SIGINT), 0);

    const auto started = Clock::now();
    const auto status  = reap( child, std::chrono::seconds( 10));

    ASSERT_TRUE( status.has_value());
    EXPECT_TRUE( WIFSIGNALED( *status)) << "the second one is the default action";
    EXPECT_EQ( WTERMSIG( *status), SIGINT);
    EXPECT_LT( Clock::now() - started, std::chrono::milliseconds( 1000)) << "it did not wait out the 1.5 s script";

    std::vector<std::string> lines = Seen;
    readLines( child, lines, []( const std::string &) { return false; }, std::chrono::seconds( 1));
    EXPECT_FALSE( summarise( lines).AllPassed.has_value()) << "no runEnd: this is the path that needs a --safe";
}

TEST_F( StopRun, StdinEndingIsAConsoleThatWentAwayAndEndsTheRunToo)
{
    auto child = startLongRun();

    ::close( child.In);
    child.In = -1;

    const auto outcome = finish( child);
    const auto status  = reap( child, std::chrono::seconds( 10));

    expectEndedTheWayARunEnds( outcome, "the console that started this run went away");
    ASSERT_TRUE( status.has_value());
    EXPECT_EQ( WEXITSTATUS( *status), 1);
}

// ---------------------------------------------------------------------------
// A stop inside core::Delay
// ---------------------------------------------------------------------------
//
// The tests above stop a run whose script is in a sleep_for, which a stop can
// only wait out. core::Delay is the verb that can be ended -- so these give the
// script a thirty-second Delay and expect the run to be over in a small
// fraction of that, with the test closed as failed rather than left open or
// passed, and the hooks and the rest of the wind-down intact.
//
namespace
{
    constexpr const char * kLongDelayVerbMs = "30000";

    class StopRunInDelay : public StopRun
    {
        protected:
            auto SetUp() -> void override
            {
                StopRun::SetUp();
                ::unsetenv( "THORIUM_FIXTURE_SCRIPT_DELAY_MS");
                ::setenv( "THORIUM_FIXTURE_SCRIPT_DELAY_VERB_MS", kLongDelayVerbMs, 1);
            }

            auto TearDown() -> void override
            {
                ::unsetenv( "THORIUM_FIXTURE_SCRIPT_DELAY_VERB_MS");
                StopRun::TearDown();
            }

            // What a stop in the middle of a long Delay must produce.
            static auto expectEndedInsideTheDelay( const Outcome & outcome, const Clock::time_point stoppedAt,
                                                    const std::string & why) -> void
            {
                EXPECT_LT( Clock::now() - stoppedAt, std::chrono::seconds( 5)) << "it did not wait out the thirty seconds";

                ASSERT_TRUE( outcome.AllPassed.has_value()) << "a stopped run still closes itself out";
                EXPECT_FALSE( *outcome.AllPassed);
                EXPECT_TRUE( hasNote( outcome, why));

                ASSERT_EQ( outcome.TestEnds, 1) << "the interrupted test was closed, not left open";
                EXPECT_FALSE( outcome.Verdicts.at( 0)) << "and closed as failed: it did not finish";
                EXPECT_EQ( outcome.OtherScripts, 0) << "nothing further began";

                ASSERT_EQ( outcome.Teardowns.size(), 2u) << "both hooks still ran";
                EXPECT_EQ( outcome.Teardowns[ 0], "group-teardown");
                EXPECT_EQ( outcome.Teardowns[ 1], "teardown");

                ASSERT_EQ( outcome.DelayDetails.size(), 2u);
                EXPECT_EQ( outcome.DelayDetails[ 0], "waiting");
                EXPECT_NE( outcome.DelayDetails[ 1].find( "interrupted by a stop request"), std::string::npos);
            }
    };
}

TEST_F( StopRunInDelay, AStopLineEndsTheTestAtOnceInsteadOfWaitingOutTheDelay)
{
    auto child = startLongRun();

    const auto stoppedAt = Clock::now();
    ASSERT_EQ( ::write( child.In, "stop\n", 5), 5);

    const auto outcome = finish( child);
    const auto status  = reap( child, std::chrono::seconds( 10));

    expectEndedInsideTheDelay( outcome, stoppedAt, "stopped by operator");
    ASSERT_TRUE( status.has_value());
    EXPECT_EQ( WEXITSTATUS( *status), 1);
}

TEST_F( StopRunInDelay, CtrlCEndsTheTestAtOnceInsteadOfWaitingOutTheDelay)
{
    auto child = startLongRun();

    const auto stoppedAt = Clock::now();
    ASSERT_EQ( ::kill( child.Pid, SIGINT), 0);

    const auto outcome = finish( child);
    const auto status  = reap( child, std::chrono::seconds( 10));

    expectEndedInsideTheDelay( outcome, stoppedAt, "stopped by operator");
    ASSERT_TRUE( status.has_value());
    EXPECT_TRUE( WIFEXITED( *status));
    EXPECT_EQ( WEXITSTATUS( *status), 1);
}

TEST_F( StopRunInDelay, TheConsoleGoingAwayEndsItToo)
{
    auto child = startLongRun();

    const auto stoppedAt = Clock::now();
    ::close( child.In);
    child.In = -1;

    const auto outcome = finish( child);

    expectEndedInsideTheDelay( outcome, stoppedAt, "the console that started this run went away");
}

// ---------------------------------------------------------------------------
// The server, supervising the same run
// ---------------------------------------------------------------------------

namespace
{
    auto freePort() -> int
    {
        const int fd = ::socket( AF_INET, SOCK_STREAM, 0);

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl( INADDR_LOOPBACK);
        address.sin_port        = 0;

        ::bind( fd, reinterpret_cast<sockaddr *>( &address), sizeof( address));

        socklen_t length = sizeof( address);
        ::getsockname( fd, reinterpret_cast<sockaddr *>( &address), &length);
        ::close( fd);

        return ntohs( address.sin_port);
    }

    // Two seconds for a server to start listening.
    auto waitForServer( int port) -> bool
    {
        const auto deadline = Clock::now() + std::chrono::seconds( 10);

        while( Clock::now() < deadline)
        {
            httplib::Client client( "127.0.0.1", port);
            client.set_connection_timeout( 0, 200000);
            if( client.Get( "/api/status")) { return true; }
            std::this_thread::sleep_for( std::chrono::milliseconds( 50));
        }

        return false;
    }

    auto escaped( const std::string & text) -> std::string
    {
        std::string out;
        for( const char c : text) { if( c == '\\' || c == '"') { out += '\\'; } out += c; }
        return out;
    }
}

class ServerStopRun : public StopRun
{
    protected:
        // A server over the hooked runner, and the long run started through it,
        // the way the page does: POST /api/run with --replay and a log directory.
        auto startServerAndRun() -> void
        {
            Port = freePort();
            LogDir = ( Dir / "logs").string();

            Server = spawn( { kServer, "--run-scripts=" + kHooked, "--port=" + std::to_string( Port)});
            ASSERT_TRUE( waitForServer( Port)) << "the server did not come up";

            const auto body =
                R"({"selection":[],"settings":[)"
                R"({"flag":"--replay","value":")" + escaped( Skeleton) + R"(","present":true},)"
                R"({"flag":"--repeat","value":"100000","present":true},)"
                R"({"flag":"--log-dir","value":")" + escaped( LogDir) + R"(","present":true}],"extra":[]})";

            httplib::Client client( "127.0.0.1", Port);
            const auto reply = client.Post( "/api/run", body, "application/json");
            ASSERT_TRUE( reply);
            ASSERT_EQ( reply->status, 200);

            // Into the middle of its first script.
            std::this_thread::sleep_for( std::chrono::milliseconds( 600));
            ASSERT_TRUE( running());
        }

        auto running() -> bool
        {
            httplib::Client client( "127.0.0.1", Port);
            const auto reply = client.Get( "/api/status");
            return reply && reply->body.find( R"("running":true)") != std::string::npos;
        }

        int          Port{ 0 };
        std::string  LogDir;
        Child        Server;
};

TEST_F( ServerStopRun, TheConsoleDyingEndsTheRunItStartedAndTheLogsAreWritten)
{
    startServerAndRun();

    // No chance to say anything: the nearest thing to a crash, a power cut to
    // the console. The run's stdin simply ends.
    ASSERT_EQ( ::kill( Server.Pid, SIGKILL), 0);
    int status = 0;
    ::waitpid( Server.Pid, &status, 0);
    Server.Pid = -1;

    //
    // The orphan is not a child of this test, so its end is seen by what it
    // leaves: a SARIF log, written only when a run closes itself out, naming
    // the reason.
    //
    const auto deadline = Clock::now() + std::chrono::seconds( 20);
    std::string sarif;

    while( Clock::now() < deadline && sarif.find( "went away") == std::string::npos)
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 100));

        if( !std::filesystem::exists( LogDir)) { continue; }

        for( const auto & entry : std::filesystem::directory_iterator( LogDir))
        {
            if( entry.path().extension() != ".sarif") { continue; }

            std::ifstream file( entry.path());
            std::stringstream text;
            text << file.rdbuf();
            sarif = text.str();
        }
    }

    EXPECT_NE( sarif.find( "the console that started this run went away"), std::string::npos)
        << "the orphaned run ended itself and said why";
}

TEST_F( ServerStopRun, EmergencyKillsTheRunAndSafesTheRigWithoutWaitingForTheTest)
{
    startServerAndRun();

    httplib::Client client( "127.0.0.1", Port);
    const auto started = Clock::now();
    const auto reply   = client.Post( "/api/emergency", "", "application/json");

    ASSERT_TRUE( reply);
    EXPECT_EQ( reply->body, R"({"killed":true,"safed":true})");
    EXPECT_LT( Clock::now() - started, std::chrono::milliseconds( 1000)) << "it did not wait out the 1.5 s script";
    EXPECT_FALSE( running());

    // Nothing is left to write a log: it was killed, which is what it costs.
    std::this_thread::sleep_for( std::chrono::milliseconds( 2000));
    const bool wroteSarif = std::filesystem::exists( LogDir) &&
        std::any_of( std::filesystem::directory_iterator( LogDir), std::filesystem::directory_iterator{},
                     []( const auto & entry) { return entry.path().extension() == ".sarif"; });
    EXPECT_FALSE( wroteSarif) << "an emergency stop's run writes no logs";
}

TEST_F( ServerStopRun, StopThroughTheServerLetsTheTestFinishThenEndsTheRun)
{
    startServerAndRun();

    httplib::Client client( "127.0.0.1", Port);
    const auto reply = client.Post( "/api/stop", "", "application/json");

    ASSERT_TRUE( reply);
    EXPECT_EQ( reply->status, 200);
    EXPECT_TRUE( running()) << "asked, not killed: its 1.5 s test is still going";

    const auto deadline = Clock::now() + std::chrono::seconds( 15);
    while( Clock::now() < deadline && running())
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 50));
    }

    EXPECT_FALSE( running()) << "the run ended once its test had";
}

#endif // !_WIN32
