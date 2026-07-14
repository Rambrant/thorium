#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/quantity_kind.hpp"
#include "core/recording.hpp"

namespace core
{
    //
    // The seam dut::Measure fetches a value through. A "live read" is always
    // available as a callback (mLiveRead below) so a session can choose to
    // perform it, ignore it entirely (ScriptedSession), or do both and log the
    // result (RecordingSession). This can't be a virtual template -- C++ has
    // no such thing -- so it works in the closed QuantityVariant instead;
    // dut::Measure is what unwraps to the caller's concrete Quantity<Unit> and
    // is where a real type mismatch becomes a compile error rather than
    // reaching this interface at all. See core/quantity_kind.hpp.
    //
    class ISession
    {
        public:
            virtual ~ISession() = default;

            //
            // liveRead performs the actual routing + instrument read (hal-level
            // work); it exists as a callback so this interface itself never
            // needs to know about hal at all. name/kind/instrumentId are passed
            // alongside purely for sessions that want to key off them
            // (ScriptedSession) or log them (RecordingSession) without also
            // having to invoke liveRead to find out what they are.
            //
            [[nodiscard]]
            virtual auto fetch(
                std::string_view                          name,
                std::string_view                          instrumentId,
                QuantityKind                               kind,
                const std::function<QuantityVariant()> &  liveRead) -> QuantityVariant = 0;
    };

    //
    // The default session: always performs the real read. Stateless -- every
    // dut::Measure call site can share one instance.
    //
    class LiveSession : public ISession
    {
        public:
            [[nodiscard]]
            auto fetch(
                std::string_view,
                std::string_view,
                QuantityKind,
                const std::function<QuantityVariant()> & liveRead) -> QuantityVariant override
            {
                return liveRead();
            }
    };

    //
    // A session that never touches hardware: it hands back pre-determined
    // values per DUT point name, so script unit tests (and later, debugging
    // playback of a real run) never need hal at all and never need a
    // recompile to switch between canned and live behavior.
    //
    // Two ways to populate it:
    //   - program(name, value): a hand-authored single value for a test. Calling
    //     it again for the same name overwrites the value -- it's meant to read
    //     as "this point is always 5.02V for this test", however many times the
    //     script under test happens to measure it.
    //   - loadFromFile(path): a strict, ordered sequence of values captured
    //     during a real run (see RecordingSession below). Each fetch for that
    //     name dequeues the next one, in the order they were recorded; asking
    //     for more values than were recorded is a hard error rather than
    //     silently repeating the last one, since that would hide the fact that
    //     the script's behavior has diverged from what was recorded.
    //
    class ScriptedSession : public ISession
    {
        public:
            //
            // Hand-authored programming for script unit tests. Overwrites
            // whatever was previously queued for this name.
            //
            auto program( std::string_view name, QuantityVariant value) -> void;

            //
            // Loads a recording (see core/recording.hpp) and queues its samples
            // per point name, in the order they were recorded.
            //
            static auto loadFromFile( const std::string & path) -> ScriptedSession;

            [[nodiscard]]
            auto fetch(
                std::string_view                         name,
                std::string_view                         instrumentId,
                QuantityKind                              kind,
                const std::function<QuantityVariant()> & liveRead) -> QuantityVariant override;

        private:
            //
            // mSticky: set by program(), always the same value, never consumed.
            // mQueue: set by loadFromFile(), consumed in order; empties out and
            // throws once exhausted. A name is in exactly one of the two maps.
            //
            std::unordered_map<std::string, QuantityVariant>       mSticky;
            std::unordered_map<std::string, std::deque<QuantityVariant>> mQueue;
    };

    //
    // Decorates any other session, recording every fetch (whether it went to
    // hardware or was itself scripted) into an in-memory log. main.cpp owns
    // one of these for the run and dumps it via core::writeRecording once
    // every script has finished, giving the "what did we actually read"
    // artifact that ScriptedSession::loadFromFile reads back in.
    //
    class RecordingSession : public ISession
    {
        public:
            explicit RecordingSession( ISession & inner) : mInner( inner) {}

            [[nodiscard]]
            auto fetch(
                std::string_view                         name,
                std::string_view                         instrumentId,
                QuantityKind                              kind,
                const std::function<QuantityVariant()> & liveRead) -> QuantityVariant override;

            [[nodiscard]]
            auto samples() const -> const std::vector<RecordedSample> &
            {
                return mSamples;
            }

        private:
            ISession &                   mInner;
            std::uint64_t                mNextSequence{ 0 };
            std::vector<RecordedSample>  mSamples;
    };
} // namespace core
