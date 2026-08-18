#pragma once

#include <chrono>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/bytes.hpp"
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

            //
            // The same seam for what a byte-oriented instrument hands back --
            // a serial reply, a frame off a bus (see core::ReadEngine in
            // core/transfer.hpp). Same contract in every respect: liveRead does
            // the real I/O and is a callback so this interface still knows
            // nothing about hal, and name/instrumentId are passed alongside for
            // sessions that key off them or log them.
            //
            // A second method rather than widening QuantityVariant to hold a
            // Bytes alternative, and the reason is the same one core::Bytes
            // exists for at all. QuantityVariant is a closed set of *numbers in
            // units*: QuantityKind indexes it, unitSymbol() answers for every
            // alternative, quantityVariantFromKind() rebuilds one from a
            // double, and asQuantity<T>() unwraps it for a criterion with an
            // epsilon. A payload satisfies none of that -- there is no unit, no
            // double to rebuild it from, and no tolerance to compare it within
            // -- so admitting it would have meant a variant alternative that
            // every one of those functions had to special-case, and a
            // QuantityKind enumerator naming something that is not a quantity.
            //
            // Pure, not defaulted to "throw" or "no payload here". Every
            // session in this file has a real answer for both questions, and a
            // default would let a future one silently answer only half of them
            // -- a script would then replay its measurements and quietly go
            // live for its serial reads, which is precisely the kind of
            // half-scripted run that makes a green result meaningless.
            //
            [[nodiscard]]
            virtual auto fetchData(
                std::string_view                  name,
                std::string_view                  instrumentId,
                const std::function<Bytes()> &    liveRead) -> Bytes = 0;
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

            [[nodiscard]]
            auto fetchData(
                std::string_view,
                std::string_view,
                const std::function<Bytes()> & liveRead) -> Bytes override
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

            [[nodiscard]]
            auto fetchData(
                const std::string_view          name,
                const std::string_view          instrumentId,
                const std::function<Bytes()> &  liveRead) -> Bytes override
            {
                return mCurrent->fetchData( name, instrumentId, liveRead);
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
    // The payload half of the same three ideas -- see ValueSource, sourceOf and
    // constantSource above, each of which this mirrors exactly. Separate rather
    // than generic over the element type purely because ValueSource is named in
    // the public shape of ScriptedSession and MeasureEngine, and a template
    // there would have made every one of those spellings carry an argument that
    // is always one of two types.
    //
    using DataSource = std::move_only_function<std::optional<Bytes>()>;

    template<std::ranges::input_range R>
        requires std::constructible_from<Bytes, std::ranges::range_reference_t<R>>
    [[nodiscard]]
    auto dataSourceOf( R range) -> DataSource
    {
        struct State
        {
            R                                          mRange;
            std::optional<std::ranges::iterator_t<R>>  mCursor;
        };

        return [ state = std::make_unique<State>( std::move( range), std::nullopt)]() mutable
            -> std::optional<Bytes>
        {
            if( !state->mCursor)
            {
                state->mCursor = std::ranges::begin( state->mRange);
            }

            if( *state->mCursor == std::ranges::end( state->mRange))
            {
                return std::nullopt;
            }

            Bytes value{ **state->mCursor };

            ++*state->mCursor;

            return value;
        };
    }

    [[nodiscard]]
    auto constantDataSource( Bytes value) -> DataSource;

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
            // The payload half, kept in its own map rather than sharing one.
            // A name is programmed for one kind of answer or the other, never
            // both, and two maps make "nothing programmed for X" a question
            // each seam answers about itself -- where one map would have made a
            // quantity programmed under a name a serial read then asked for a
            // type confusion to diagnose at fetch time instead of a plain
            // absence.
            //
            auto programData( std::string_view name, Bytes value) -> void;
            auto programData( std::string_view name, DataSource source) -> void;

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

            [[nodiscard]]
            auto fetchData(
                std::string_view                name,
                std::string_view                instrumentId,
                const std::function<Bytes()> &  liveRead) -> Bytes override;

        private:
            std::unordered_map<std::string, ValueSource>  mSources;
            std::unordered_map<std::string, DataSource>   mDataSources;
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
            auto fetchData(
                std::string_view                name,
                std::string_view                instrumentId,
                const std::function<Bytes()> &  liveRead) -> Bytes override;

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

    //
    // The four sessions a run reads through, and the switching between them --
    // one object, so that more than one verb can share it.
    //
    // This used to be five members of core::MeasureEngine, and moved out for a
    // reason worth stating plainly: Measure is no longer the only verb that
    // observes something. core::ReadEngine (see core/transfer.hpp) fetches a
    // byte payload through the very same seam, and the two have to share *one*
    // bank rather than owning one each.
    //
    // Not for tidiness -- for correctness of replay. A recording is an ordered
    // stream, and order is the whole of what makes a replay faithful (see
    // core::RecordedSample). Two engines each holding their own RecordingSession
    // would produce two independent sequences, each numbered from zero, with no
    // way to reconstruct that a run measured a rail, read a reply, then measured
    // again. Worse, --replay would arm one of them and not the other, so a
    // replayed run would take its voltages from the file and its serial replies
    // from whatever hardware happened to be attached: a green result that means
    // nothing at all.
    //
    // Still an owned object passed by reference rather than a process-wide
    // singleton like core::journal(). The journal is genuinely global because
    // every verb in the framework posts to it and a test script has no
    // parameter to carry one through; a session bank is reached by exactly the
    // verbs that read, all of which are constructed in one place, and keeping
    // it an ordinary object is what lets a unit test build an engine with a
    // bank of its own (see libs/core/tests/test_measure.cpp) instead of
    // contending for shared state.
    //
    class SessionBank
    {
        public:
            //
            // Feeds a canned value for a point, bypassing hal entirely -- for
            // script unit tests. Switches to scripted values if not already
            // there (no separate "now use this" call); useLive() goes back and
            // discards them again.
            //
            auto inject( const std::string_view pointName, QuantityVariant value) -> void
            {
                mScripted.program( pointName, std::move( value));
                mSwitchable.use( mScripted);
            }

            auto inject( const std::string_view pointName, ValueSource source) -> void
            {
                mScripted.program( pointName, std::move( source));
                mSwitchable.use( mScripted);
            }

            //
            // The payload half. Deliberately a different name rather than an
            // overload: Bytes is constructible from a string literal, so
            // inject( "Console", "ACK\r") would otherwise be ambiguous with
            // nothing in the call to say which seam was meant.
            //
            auto injectData( const std::string_view name, Bytes value) -> void
            {
                mScripted.programData( name, std::move( value));
                mSwitchable.use( mScripted);
            }

            auto injectData( const std::string_view name, DataSource source) -> void
            {
                mScripted.programData( name, std::move( source));
                mSwitchable.use( mScripted);
            }

            //
            // Loads a recording and replays it in place of injected values --
            // discarding whatever was previously injected or loaded. One file
            // arms both seams, which is the point: see this class's own comment
            // on why a half-armed replay is the failure mode to rule out.
            //
            auto load( const std::string & path) -> void
            {
                mScripted = ScriptedSession::loadFromFile( path);
                mSwitchable.use( mScripted);
            }

            //
            // Returns to real routing + real instrument reads, and discards
            // whatever was injected or loaded -- the full inverse of inject()
            // and load(), not half of one. See core::MeasureEngine::useLive's
            // own comment for what a partial version of this let through.
            //
            auto useLive() -> void
            {
                mSwitchable.useDefault();

                mScripted = ScriptedSession{};
            }

            auto startRecording() -> void { mRecording = true;  }
            auto stopRecording()  -> void { mRecording = false; }

            auto dump( std::ostream & out) const -> void
            {
                writeRecording( out, mRecorder.samples());
            }

            [[nodiscard]]
            auto samples() const -> const std::vector<RecordedSample> &
            {
                return mRecorder.samples();
            }

            //
            // Whichever session a fetch should go through right now -- the
            // recorder when recording (it decorates the switchable one), the
            // switchable one otherwise.
            //
            [[nodiscard]]
            auto active() -> ISession &
            {
                return mRecording ? static_cast<ISession &>( mRecorder) : static_cast<ISession &>( mSwitchable);
            }

        private:
            LiveSession        mLive;
            ScriptedSession    mScripted;
            SwitchableSession  mSwitchable{ mLive };
            RecordingSession   mRecorder{ mSwitchable };
            bool               mRecording{ false };
    };
} // namespace core
