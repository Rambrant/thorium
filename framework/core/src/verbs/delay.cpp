#include "core/verbs/delay.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "core/control/stop_request.hpp"
#include "core/journal/journal.hpp"
#include "core/session/bench.hpp"

namespace core
{
    namespace
    {
        using Seconds = std::chrono::duration<double>;

        // "250 ms", "1.5 s", "30 s" -- the precision a person writes a delay in.
        [[nodiscard]]
        auto describeDelay( const Seconds length) -> std::string
        {
            const double seconds = length.count();
            char         buffer[ 32];

            if( seconds < 1.0)
                std::snprintf( buffer, sizeof( buffer), "%g ms", std::round( seconds * 1.0e6) / 1.0e3);
            else
                std::snprintf( buffer, sizeof( buffer), "%g s", std::round( seconds * 1.0e3) / 1.0e3);

            return buffer;
        }

        auto postDelay( const std::string_view reason, const std::string & detail, const Seconds length) -> void
        {
            journal().post( JournalRecord{
                .Method  = Verb::Delay,
                .Subject = std::string( reason.empty() ? std::string_view( "delay") : reason),
                .Detail  = detail,
                .Value   = describeDelay( length),
                .Numeric = length.count(),
                .Unit    = "s"
            });
        }

        // Short enough that a stop is felt as immediate, long enough that
        // polling is not what the delay spends its time on.
        constexpr auto kSlice = std::chrono::milliseconds( 20);
    } // namespace

    auto DelayEngine::operator()( const quantities::Time duration, const std::string_view reason) const -> void
    {
        const Seconds length{ duration.value() < 0.0 ? 0.0 : duration.value() };

        if( !bench().isAttached())
        {
            postDelay( reason, "skipped: no bench attached", length);

            return;
        }

        postDelay( reason, "waiting", length);

        const auto start    = std::chrono::steady_clock::now();
        const auto deadline = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>( length);

        for( ;;)
        {
            if( stopArmed() && stopRequested())
            {
                const Seconds elapsed = std::chrono::steady_clock::now() - start;

                postDelay( reason, "interrupted by a stop request after " + describeDelay( elapsed), length);

                throw StopRequested{};
            }

            const auto now = std::chrono::steady_clock::now();

            if( now >= deadline)
                return;

            std::this_thread::sleep_for( std::min<std::chrono::steady_clock::duration>( kSlice, deadline - now));
        }
    }

    const DelayEngine Delay{};
} // namespace core
