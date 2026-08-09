#pragma once

#include <chrono>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
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
    // A session that forwards to whichever other session it currently holds,
    // defaulting to the one it was constructed with. This is what lets a
    // catalog script's Bench -- necessarily a global, since a TestScript
    // takes no parameters at all, and so no device one either (see
    // core/test_catalog.hpp) -- be pointed at a ScriptedSession for the
    // duration of one test and back again, with no recompile and no change to
    // the script itself.
    //
    class SwitchableSession : public ISession
    {
        public:
            explicit SwitchableSession( ISession & defaultSession) : mDefault( defaultSession), mCurrent( &defaultSession) {}

            // Points this session at another session until use() or useDefault() is called again.
            auto use( ISession & session) -> void
            {
                mCurrent = &session;
            }

            // Restores whichever session this was constructed with.
            auto useDefault() -> void
            {
                mCurrent = &mDefault;
            }

            [[nodiscard]]
            auto fetch(
                const std::string_view                    name,
                const std::string_view                    instrumentId,
                const QuantityKind                        kind,
                const std::function<QuantityVariant()> &  liveRead) -> QuantityVariant override
            {
                return mCurrent->fetch( name, instrumentId, kind, liveRead);
            }

        private:
            ISession &  mDefault;
            ISession *  mCurrent;
    };

    //
    // Where a scripted point's values come from: call it for the next one,
    // std::nullopt once there are no more.
    //
    // One seam for every way of producing values, because "how the next value
    // is chosen" is an algorithm, and the caller owns algorithms -- a fixed
    // list, a sweep, a random walk, a value read out of a file. Anything a
    // std::generator can co_yield fits here, which is the point: adding a new
    // shape of test stimulus means writing a coroutine at the call site, not
    // extending this header with another kind of range and another exhaustion
    // rule to go with it.
    //
    // That optional is also what retires "what should happen when the values
    // run out" as a question this header has to answer. It is the source's own
    // business: a finite list ends and ScriptedSession::fetch throws (see
    // core/src/session.cpp), while an infinite one -- a random source, or one
    // that deliberately cycles -- simply never returns nullopt and never
    // exhausts. Neither behaviour is privileged here.
    //
    // std::move_only_function rather than std::function, and not by preference:
    // a lambda owning a std::generator is move-only, and std::function requires
    // a copyable callable, so std::function cannot hold the very thing this
    // exists to hold.
    //
    using ValueSource = std::move_only_function<std::optional<QuantityVariant>()>;

    //
    // A source that yields one value per element of range, in order, then
    // exhausts. Accepts anything that models an input range whose elements a
    // QuantityVariant can be built from -- a vector or array of Voltage, a
    // views pipeline, or a std::generator<Voltage>.
    //
    // The range is held behind a unique_ptr rather than captured directly, so
    // that moving the resulting ValueSource -- which happens whenever one is
    // stored, reassigned, or the map holding it rehashes -- never moves the
    // range itself. An iterator whose range moved out from under it is exactly
    // the kind of dangling this seam must not have, and one pointer hop per
    // value is nothing against a measurement.
    //
    // begin() is called lazily, on the first value asked for rather than here.
    // That is required, not tidiness: a std::generator's begin() runs the
    // coroutine up to its first co_yield, and a source that is built but never
    // fetched from must not have run any of the caller's algorithm.
    //
    template<std::ranges::input_range R>
        requires std::constructible_from<QuantityVariant, std::ranges::range_reference_t<R>>
    [[nodiscard]]
    auto sourceOf( R range) -> ValueSource
    {
        struct State
        {
            R                                          mRange;
            std::optional<std::ranges::iterator_t<R>>  mCursor;
        };

        return [ state = std::make_unique<State>( std::move( range), std::nullopt)]() mutable
            -> std::optional<QuantityVariant>
        {
            if( !state->mCursor)
            {
                state->mCursor = std::ranges::begin( state->mRange);
            }

            if( *state->mCursor == std::ranges::end( state->mRange))
            {
                return std::nullopt;
            }

            QuantityVariant value{ **state->mCursor };

            //
            // Deref-then-pre-increment rather than *it++: a std::generator's
            // iterator is an input iterator whose post-increment returns void,
            // so the usual one-liner does not compile for the case this
            // function exists to support.
            //
            ++*state->mCursor;

            return value;
        };
    }

    //
    // A source that yields the same value forever and never exhausts -- what
    // program(name, value) below is built from.
    //
    [[nodiscard]]
    auto constantSource( QuantityVariant value) -> ValueSource;

    //
    // A session that never touches hardware: it hands back pre-determined
    // values per DUT point name, so script unit tests (and later, debugging
    // playback of a real run) never need hal at all and never need a
    // recompile to switch between canned and live behavior.
    //
    // Every way of populating it is a ValueSource above; there is deliberately
    // no second storage shape beside them. program(name, value) is a constant
    // source, loadFromFile is a source over the recorded samples, and a caller
    // handing in its own generator is the same mechanism again -- so "what
    // happens when the values run out" has exactly one answer (fetch throws
    // when a source returns nullopt) instead of one per population method.
    //
    // Three ways to populate it:
    //   - program(name, value): a hand-authored single value. Calling it again
    //     for the same name replaces whatever was there -- it reads as "this
    //     point is always 5.02V for this test", however many times the script
    //     under test happens to measure it, because a constant source never
    //     exhausts.
    //   - program(name, source): any sequence at all; see ValueSource.
    //   - loadFromFile(path): a strict, ordered sequence of values captured
    //     during a real run (see RecordingSession below). Each fetch for that
    //     name takes the next one, in the order they were recorded; asking for
    //     more values than were recorded is a hard error rather than silently
    //     repeating the last one, since that would hide the fact that the
    //     script's behavior has diverged from what was recorded.
    //
    // Move-only, since a ValueSource is. Nothing copies a session --
    // loadFromFile hands one back by value and MeasureEngine move-assigns it
    // (see core/measure.hpp) -- so this costs nothing that was being used.
    //
    class ScriptedSession : public ISession
    {
        public:
            //
            // Hand-authored programming for script unit tests. Both overloads
            // replace whatever was previously programmed for this name.
            //
            auto program( std::string_view name, QuantityVariant value) -> void;
            auto program( std::string_view name, ValueSource source) -> void;

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
            std::unordered_map<std::string, ValueSource>  mSources;
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
