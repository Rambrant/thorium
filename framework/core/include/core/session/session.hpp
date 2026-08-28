#pragma once

#include <chrono>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/session/bench.hpp"
#include "core/quantities/bytes.hpp"
#include "core/quantities/quantity_kind.hpp"
#include "core/session/recording.hpp"
#include "core/quantities/waveform.hpp"

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
    // reaching this interface at all. See core/quantities/quantity_kind.hpp.
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
            // core/verbs/transfer.hpp). Same contract in every respect: liveRead does
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

            //
            // And the same seam again for a yes/no observation: whether a
            // triggered acquisition completed within the time it was given
            // (see core::AwaitEngine in core/verbs/acquire.hpp). Same contract as
            // both of the above -- liveRead does the real polling, and the
            // name/instrumentId are alongside for sessions that key off them.
            //
            // A third method rather than a third alternative in
            // QuantityVariant, for the reason fetchData's own comment gives
            // and one more besides. Waiting is not measuring: the answer has
            // no unit, no tolerance and no meaningful arithmetic, and the
            // *question* is not "what is the value at this point" but "did the
            // thing I armed happen". Keying it through fetch() would have made
            // a completion flag indexable by QuantityKind, which is a promise
            // core/quantities/quantity_kind.hpp could not keep.
            //
            // Pure, like the other two, and for the same reason: a future
            // session answering only two of the three questions would replay
            // its measurements and then ask absent hardware whether there had
            // been anything to measure.
            //
            [[nodiscard]]
            virtual auto fetchFlag(
                std::string_view                  name,
                std::string_view                  instrumentId,
                const std::function<bool()> &     liveRead) -> bool = 0;

            //
            // And the seam a fourth time, for a captured trace -- the whole
            // record an instrument holds after an acquisition (see
            // core::FetchEngine in core/verbs/trace.hpp). Same contract as the other
            // three in every respect.
            //
            // A fourth method rather than a fourth QuantityVariant alternative,
            // and the argument is fetchData's applied to a different shape: a
            // trace is not one number in one unit. It has a length, its
            // elements have positions, the positions are times, and the
            // criterion machinery underneath asQuantity<T>() -- an epsilon, a
            // comparison, a single numeric column in both logs -- has nothing
            // to do with any of that. What a script checks is a *reduction* of
            // a trace, and a reduction is a QuantityVariant already.
            //
            // Pure, like the other three, and for the reason that has now been
            // given three times and is worth the fourth: a session answering
            // only some of these questions would replay part of a run and go
            // live for the rest.
            //
            [[nodiscard]]
            virtual auto fetchTrace(
                std::string_view                  name,
                std::string_view                  instrumentId,
                const std::function<Waveform()> & liveRead) -> Waveform = 0;
    };

    //
    // The default session: always performs the real read. Stateless -- every
    // dut::Measure call site can share one instance.
    //
    // "Always" up to one thing, which is what keeps core/session/bench.hpp's guarantee
    // total rather than conventional. Every instructing verb skips its driver
    // when no bench is attached; this is the last path by which a *reading*
    // could still reach one, and it is closed by refusing rather than by
    // substituting. There is no honest value to hand back -- a zero would be a
    // number no instrument produced, sitting in a log beside a criterion that
    // then passes or fails on it.
    //
    // A combination framework/runner/src/main.cpp cannot produce: it detaches exactly for
    // the three modes that supply their own readings, so a detached run always
    // has a scripted session in front of this one. What this catches is the
    // in-process caller who detached and then did not arm anything -- an error,
    // and one whose only other symptom would be a driver call nobody meant to
    // make.
    //
    class LiveSession : public ISession
    {
        public:
            [[nodiscard]]
            auto fetch(
                const std::string_view                    name,
                std::string_view,
                QuantityKind,
                const std::function<QuantityVariant()> &  liveRead) -> QuantityVariant override
            {
                requireBench( name);

                return liveRead();
            }

            [[nodiscard]]
            auto fetchData(
                const std::string_view          name,
                std::string_view,
                const std::function<Bytes()> &  liveRead) -> Bytes override
            {
                requireBench( name);

                return liveRead();
            }

            [[nodiscard]]
            auto fetchFlag(
                const std::string_view         name,
                std::string_view,
                const std::function<bool()> &  liveRead) -> bool override
            {
                requireBench( name);

                return liveRead();
            }

            [[nodiscard]]
            auto fetchTrace(
                const std::string_view             name,
                std::string_view,
                const std::function<Waveform()> &  liveRead) -> Waveform override
            {
                requireBench( name);

                return liveRead();
            }

        private:
            static auto requireBench( const std::string_view name) -> void
            {
                if( !bench().isAttached())
                {
                    throw std::runtime_error(
                        "LiveSession: '" + std::string( name) +
                        "' would be read from an instrument, but no bench is attached -- inject or replay it,"
                        " or attach a bench");
                }
            }
    };

    //
    // A session that answers every question with a placeholder and touches
    // nothing -- what --skeleton runs the catalog against.
    //
    // The point is not the answers, which are worthless; it is the *keys*. A
    // script's session keys are not written down anywhere a person can read
    // them: "Output5V.Vbase", "Osc1.Acquisition", "Ser1.Data", "AcP1.A.Voltage"
    // are produced by core::Port::qualifiedBy, by an engine's
    // "<instrument>.<what>" rule and by the DUT adapter, and the only complete
    // and correct list of them is what the scripts actually ask for. Running
    // them against this session and recording the result produces exactly that
    // list, in order, as a file that is already a valid recording -- so
    // authoring a replay becomes editing a value column rather than
    // reconstructing a schema by reading every script.
    //
    // The placeholders are chosen for path coverage, not for plausibility, and
    // each is the answer that keeps a script going furthest:
    //
    //   - a reading is zero. Honest (this session measured nothing) and inert;
    //     a NaN would propagate into every comparison downstream and could send
    //     a script down a branch no real run would take.
    //   - a capture completed. The alternative sends every script that checks
    //     its Await straight to a Fail and past the readings it would have
    //     taken, which is the one branch that loses keys.
    //   - a payload is empty, and a trace has no samples. Neither has a
    //     "keeps going" value to choose: any bytes at all would be a guess at a
    //     protocol this session knows nothing about.
    //
    // Which is also the honest limitation, and it is worth stating wherever
    // this is offered: a script whose control flow depends on what it read has
    // more than one path through it, and a skeleton is one path -- the one
    // these answers produce. It is a starting point that is complete for the
    // straight-line case and a first approximation otherwise.
    //
    class PlaceholderSession : public ISession
    {
        public:
            [[nodiscard]]
            auto fetch(
                std::string_view,
                std::string_view,
                const QuantityKind                        kind,
                const std::function<QuantityVariant()> &) -> QuantityVariant override
            {
                return quantityVariantFromKind( kind, 0.0);
            }

            [[nodiscard]]
            auto fetchData(
                std::string_view,
                std::string_view,
                const std::function<Bytes()> &) -> Bytes override
            {
                return Bytes{};
            }

            [[nodiscard]]
            auto fetchFlag(
                std::string_view,
                std::string_view,
                const std::function<bool()> &) -> bool override
            {
                return true;
            }

            [[nodiscard]]
            auto fetchTrace(
                std::string_view,
                std::string_view,
                const std::function<Waveform()> &) -> Waveform override
            {
                return Waveform{};
            }
    };

    //
    // A session that forwards to whichever other session it currently holds,
    // defaulting to the one it was constructed with. This is what lets a
    // catalog script's Bench -- necessarily a global, since a TestScript
    // takes no parameters at all, and so no device one either (see
    // core/catalog/test_catalog.hpp) -- be pointed at a ScriptedSession for the
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

            [[nodiscard]]
            auto fetchFlag(
                const std::string_view         name,
                const std::string_view         instrumentId,
                const std::function<bool()> &  liveRead) -> bool override
            {
                return mCurrent->fetchFlag( name, instrumentId, liveRead);
            }

            [[nodiscard]]
            auto fetchTrace(
                const std::string_view             name,
                const std::string_view             instrumentId,
                const std::function<Waveform()> &  liveRead) -> Waveform override
            {
                return mCurrent->fetchTrace( name, instrumentId, liveRead);
            }

        private:
            ISession &  mDefault;
            ISession *  mCurrent;
    };

    namespace detail
    {
        //
        // The body of sourceOf, dataSourceOf and flagSourceOf below, which
        // differ in nothing at all but the element type they yield.
        //
        // Written once here rather than three times there, and that is a
        // change of mind worth recording: the three public spellings used to
        // carry three copies of this, on the argument that making them one
        // template would put an element-type parameter into ScriptedSession's
        // public shape and into every engine that names a source. That
        // argument still holds -- and it is an argument about the *aliases*,
        // not about their bodies. The aliases stay exactly as they were; only
        // the duplicated implementation moved, into a namespace no call site
        // spells. A fourth element type is coming (a captured trace, see
        // core/session/recording.hpp), and three copies was the last point at which
        // copying was cheaper than reading.
        //
        // Every reason the original body gives for its shape is unchanged and
        // still applies: the range lives behind a unique_ptr so that moving
        // the source -- which happens whenever one is stored, reassigned, or a
        // map holding it rehashes -- never moves the range out from under an
        // iterator into it; and begin() is called lazily, on the first value
        // asked for, because a std::generator's begin() runs the caller's
        // coroutine up to its first co_yield and a source that is built but
        // never fetched from must not have run any of it.
        //
        template<typename T, std::ranges::input_range R>
        [[nodiscard]]
        auto rangeSource( R range) -> std::move_only_function<std::optional<T>()>
        {
            struct State
            {
                R                                          mRange;
                std::optional<std::ranges::iterator_t<R>>  mCursor;
            };

            return [ state = std::make_unique<State>( std::move( range), std::nullopt)]() mutable
                -> std::optional<T>
            {
                if( !state->mCursor)
                {
                    state->mCursor = std::ranges::begin( state->mRange);
                }

                if( *state->mCursor == std::ranges::end( state->mRange))
                {
                    return std::nullopt;
                }

                T value{ **state->mCursor };

                //
                // Deref-then-pre-increment rather than *it++: a
                // std::generator's iterator is an input iterator whose
                // post-increment returns void, so the usual one-liner does not
                // compile for the case this function exists to support.
                //
                ++*state->mCursor;

                return value;
            };
        }

        //
        // The three words a slot needs in order to explain itself when it has
        // nothing left to give.
        //
        // Carried as data beside each slot rather than baked into the template,
        // because the diagnosis is the whole value of these messages and a
        // generic one would be worse than the three it replaces. "The script is
        // reading it more times than expected" and "the script is measuring it
        // more times than expected" send a reader to different lines of a
        // script; "the script is fetching it more times than expected" sends
        // them nowhere.
        //
        struct SlotWords
        {
            std::string_view  Noun;     // "value" / "payload" / "flag"
            std::string_view  Program;  // which program* call fills this slot
            std::string_view  Action;   // what a script does to consume one
        };

        //
        // One seam's worth of scripted answers: a source per name, and the two
        // ways asking for one can fail.
        //
        // ScriptedSession held three copies of this -- three maps, three
        // insert_or_assign pairs, three find-then-call-then-throw bodies
        // differing only in their nouns. The nouns are what SlotWords carries,
        // so the behaviour is stated once and the diagnosis stays as specific
        // as it was.
        //
        // Deliberately not part of ISession's shape and deliberately not
        // public. ScriptedSession still offers program/programData/programFlag
        // and still overrides fetch/fetchData/fetchFlag one for one -- a caller
        // cannot tell this type exists, which is what makes it safe for it to
        // be generic where those three are not (see ValueSource below on why
        // they are not).
        //
        template<typename T>
        class ObservationSlots
        {
            public:
                using Source = std::move_only_function<std::optional<T>()>;

                explicit ObservationSlots( const SlotWords words) : mWords( words) {}

                auto program( const std::string_view name, Source source) -> void
                {
                    mSources.insert_or_assign( std::string( name), std::move( source));
                }

                //
                // The next answer for this name, or a std::runtime_error saying
                // which of the two things went wrong.
                //
                // A source that has run out is a hard error rather than a
                // silent repeat of its last answer: a script consuming a name
                // more times than the test authored answers for it has diverged
                // from what was recorded, and that is precisely the thing worth
                // failing on. A source that should never run out simply never
                // returns nullopt -- see ValueSource below.
                //
                [[nodiscard]]
                auto next( const std::string_view name) -> T
                {
                    const auto key   = std::string( name);
                    const auto entry = mSources.find( key);

                    if( entry == mSources.end())
                    {
                        throw std::runtime_error(
                            "ScriptedSession: nothing programmed for '" + key + "' -- " +
                            std::string( mWords.Program) + " it, or load a recording that covers it");
                    }

                    auto value = entry->second();

                    if( !value)
                    {
                        throw std::runtime_error(
                            "ScriptedSession: no programmed " + std::string( mWords.Noun) + " left for '" + key +
                            "' -- either " + std::string( mWords.Program) +
                            " more, load a longer recording, or the script is " + std::string( mWords.Action) +
                            " it more times than expected");
                    }

                    return std::move( *value);
                }

            private:
                std::unordered_map<std::string, Source>  mSources;
                SlotWords                                mWords;
        };
    } // namespace detail

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
    // core/src/session/session.cpp), while an infinite one -- a random source, or one
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
        return detail::rangeSource<QuantityVariant>( std::move( range));
    }

    //
    // A source that yields the same value forever and never exhausts -- what
    // program(name, value) below is built from.
    //
    [[nodiscard]]
    auto constantSource( QuantityVariant value) -> ValueSource;

    //
    // The payload half of the same three ideas -- see ValueSource, sourceOf and
    // constantSource above, each of which this mirrors exactly. Named
    // separately rather than made generic over the element type because
    // ValueSource is named in the public shape of ScriptedSession and
    // MeasureEngine; the body is shared (detail::rangeSource) even though the
    // name is not.
    //
    using DataSource = std::move_only_function<std::optional<Bytes>()>;

    template<std::ranges::input_range R>
        requires std::constructible_from<Bytes, std::ranges::range_reference_t<R>>
    [[nodiscard]]
    auto dataSourceOf( R range) -> DataSource
    {
        return detail::rangeSource<Bytes>( std::move( range));
    }

    [[nodiscard]]
    auto constantDataSource( Bytes value) -> DataSource;

    //
    // And the flag half -- the same three ideas a third time, for the
    // yes/no observations core::ISession::fetchFlag answers.
    //
    // Spelled out as its own alias rather than made generic over the element
    // type, and that is still the right call: one template here would put an
    // element-type argument into ScriptedSession's public shape, into
    // SessionBank's, and into every engine that names one of these -- three
    // spellings gaining a parameter that is always one of a few known types.
    //
    // The *bodies* behind the three are another matter, and they are no longer
    // written out three times: they delegate to detail::rangeSource at the top
    // of this file. Naming the seam separately and implementing it separately
    // were never the same decision, and only the first one was worth the
    // repetition.
    //
    using FlagSource = std::move_only_function<std::optional<bool>()>;

    template<std::ranges::input_range R>
        requires std::constructible_from<bool, std::ranges::range_reference_t<R>>
    [[nodiscard]]
    auto flagSourceOf( R range) -> FlagSource
    {
        return detail::rangeSource<bool>( std::move( range));
    }

    [[nodiscard]]
    auto constantFlagSource( bool value) -> FlagSource;

    //
    // And the trace half -- the same three ideas a fourth time, for the
    // captured records core::ISession::fetchTrace answers.
    //
    using TraceSource = std::move_only_function<std::optional<Waveform>()>;

    template<std::ranges::input_range R>
        requires std::constructible_from<Waveform, std::ranges::range_reference_t<R>>
    [[nodiscard]]
    auto traceSourceOf( R range) -> TraceSource
    {
        return detail::rangeSource<Waveform>( std::move( range));
    }

    [[nodiscard]]
    auto constantTraceSource( Waveform value) -> TraceSource;

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
    // (see core/verbs/measure.hpp) -- so this costs nothing that was being used.
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
            // The payload half, kept in its own slot map rather than sharing
            // one -- see this class's private section for why the maps stay
            // separate.
            //
            auto programData( std::string_view name, Bytes value) -> void;
            auto programData( std::string_view name, DataSource source) -> void;

            //
            // The flag half, in a third slot map for the same reason the
            // payload half has a second one.
            //
            auto programFlag( std::string_view name, bool value) -> void;
            auto programFlag( std::string_view name, FlagSource source) -> void;

            //
            // The trace half, in a fourth slot map for the same reason.
            //
            auto programTrace( std::string_view name, Waveform value) -> void;
            auto programTrace( std::string_view name, TraceSource source) -> void;

            //
            // Loads a recording (see core/session/recording.hpp) and queues its samples
            // per point name, in the order they were recorded.
            //
            // selection names the catalog tests to take rows from; empty means
            // all of them, the same convention --select itself uses (see
            // isSelected in framework/runner/src/main.cpp). It is what makes a whole run's
            // recording usable for debugging one script: without it, a replayed
            // test dequeues from the front of each point's queue and so takes
            // whatever the *first* test to touch that point recorded. Two tests
            // measuring one rail is all it takes, and the result is not an
            // error -- it is a green verdict about the wrong numbers.
            //
            // Rows carrying kRunScope are kept whatever the selection says.
            // They are what the hooks read -- RUN_SETUP/RUN_TEARDOWN around the
            // selection, and a group's own SETUP/TEARDOWN around the tests of
            // whichever groups the selection reaches (see runHook in
            // framework/runner/src/main.cpp) -- so filtering them out would leave a replay
            // of one test unable to power up the rig it is not touching.
            //
            // A selection that matches no recorded test is refused rather than
            // replayed as nothing: it means this recording is of a run that
            // never included the test being asked for, and the alternative is a
            // first Measure that fails complaining about a point name, which
            // names the symptom and not the cause.
            //
            static auto loadFromFile(
                const std::string &                     path,
                const std::vector<std::string_view> &   selection = {}) -> ScriptedSession;

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
            auto fetchFlag(
                std::string_view               name,
                std::string_view               instrumentId,
                const std::function<bool()> &  liveRead) -> bool override;

            [[nodiscard]]
            auto fetchTrace(
                std::string_view                   name,
                std::string_view                   instrumentId,
                const std::function<Waveform()> &  liveRead) -> Waveform override;

        private:
            //
            // One slot map per seam, each carrying the words it explains itself
            // with -- see detail::ObservationSlots and detail::SlotWords at the
            // top of this file.
            //
            // Three members rather than one, still: a name is programmed for
            // one kind of answer or the other and never both, and separate maps
            // make "nothing programmed for X" a question each seam answers about
            // itself. One map would have made a quantity programmed under a name
            // a serial read then asked for, which is a type confusion to
            // diagnose at fetch time rather than the plain absence it is.
            //
            detail::ObservationSlots<QuantityVariant>  mSources{     { "value",   "program()",     "measuring" } };
            detail::ObservationSlots<Bytes>            mDataSources{ { "payload", "programData()", "reading"   } };
            detail::ObservationSlots<bool>             mFlagSources{ { "flag",    "programFlag()", "awaiting"  } };
            detail::ObservationSlots<Waveform>         mTraceSources{{ "trace",   "programTrace()","capturing" } };
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

            //
            // Writes each sample out as it is observed rather than keeping it,
            // through a writer the caller owns (see core::RecordingWriter).
            //
            // The in-memory default is what a unit test wants -- it asserts on
            // the samples afterwards, and there is no file. It is not what a
            // run wants once an observation can be large: a hundred captured
            // traces held until the last script finishes is a hundred traces in
            // memory, and the run most in need of its recording is the long
            // soak most likely to be killed before it reaches the end. What has
            // been streamed is on disk already.
            //
            // One direction only -- there is no stopStreaming(). A recording
            // that was half streamed and half accumulated is two partial
            // recordings, and the second half would be written after the first
            // and out of order.
            //
            auto streamTo( RecordingWriter & writer) -> void
            {
                mWriter = &writer;
            }

            [[nodiscard]]
            auto isStreaming() const -> bool
            {
                return mWriter != nullptr;
            }

            //
            // How many observations have gone through, whichever way they went.
            // samples().size() answers this only when nothing is streaming, and
            // a caller reporting on the file it just wrote needs the number in
            // both cases.
            //
            [[nodiscard]]
            auto recordedCount() const -> std::uint64_t
            {
                return mNextSequence;
            }

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
            auto fetchFlag(
                std::string_view               name,
                std::string_view               instrumentId,
                const std::function<bool()> &  liveRead) -> bool override;

            [[nodiscard]]
            auto fetchTrace(
                std::string_view                   name,
                std::string_view                   instrumentId,
                const std::function<Waveform()> &  liveRead) -> Waveform override;

            //
            // Empty while streaming, and that is not an oversight to be worked
            // around: the samples went to the writer and are not here to be
            // handed back. core::SessionBank::dump refuses rather than writing
            // an empty recording over a full one.
            //
            [[nodiscard]]
            auto samples() const -> const std::vector<RecordedSample> &
            {
                return mSamples;
            }

        private:
            //
            // The one place a sample is stamped and filed, so the three seams
            // above cannot drift apart on the two things that make a recording
            // replayable: the sequence number, and that all three go into one
            // ordered stream. See core::SessionBank on why that is a
            // correctness requirement rather than tidiness.
            //
            auto record( std::string_view name, std::string_view instrumentId, RecordedValue value) -> void;

            ISession &                   mInner;
            std::uint64_t                mNextSequence{ 0 };
            std::vector<RecordedSample>  mSamples;
            RecordingWriter *            mWriter{ nullptr };
    };

    //
    // The four sessions a run reads through, and the switching between them --
    // one object, so that more than one verb can share it.
    //
    // This used to be five members of core::MeasureEngine, and moved out for a
    // reason worth stating plainly: Measure is no longer the only verb that
    // observes something. core::ReadEngine (see core/verbs/transfer.hpp) fetches a
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
    // bank of its own (see framework/core/tests/test_measure.cpp) instead of
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
            // The flag half. Named apart from the other two for the reason
            // injectData gives, and then some: bool is the type nearly
            // anything converts to, so an overload here would have quietly
            // swallowed calls meant for either of the others.
            //
            auto injectFlag( const std::string_view name, const bool value) -> void
            {
                mScripted.programFlag( name, value);
                mSwitchable.use( mScripted);
            }

            auto injectFlag( const std::string_view name, FlagSource source) -> void
            {
                mScripted.programFlag( name, std::move( source));
                mSwitchable.use( mScripted);
            }

            //
            // The trace half, named apart from the other three for the reason
            // injectData and injectFlag give.
            //
            auto injectTrace( const std::string_view name, Waveform value) -> void
            {
                mScripted.programTrace( name, std::move( value));
                mSwitchable.use( mScripted);
            }

            auto injectTrace( const std::string_view name, TraceSource source) -> void
            {
                mScripted.programTrace( name, std::move( source));
                mSwitchable.use( mScripted);
            }

            //
            // Loads a recording and replays it in place of injected values --
            // discarding whatever was previously injected or loaded. One file
            // arms both seams, which is the point: see this class's own comment
            // on why a half-armed replay is the failure mode to rule out.
            //
            auto load( const std::string & path, const std::vector<std::string_view> & selection = {}) -> void
            {
                mScripted = ScriptedSession::loadFromFile( path, selection);
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

            //
            // Answers everything with a placeholder and touches nothing -- see
            // core::PlaceholderSession for what the answers are and why. What
            // --skeleton arms before running the catalog.
            //
            // Discards whatever was injected or loaded, like useLive: a run
            // that is half placeholder and half real values would produce a
            // skeleton that is missing exactly the keys the real values were
            // covering.
            //
            auto usePlaceholders() -> void
            {
                mScripted = ScriptedSession{};

                mSwitchable.use( mPlaceholder);
            }

            //
            // Records into memory, to be written out with dump() at the end --
            // what a unit test uses, and what a caller with nowhere to stream
            // to uses.
            //
            auto startRecording() -> void { mRecording = true;  }

            //
            // Records straight through the writer instead, one row per
            // observation as it happens. What a real run uses: see
            // core::RecordingSession::streamTo.
            //
            auto startRecording( RecordingWriter & writer) -> void
            {
                mRecorder.streamTo( writer);

                mRecording = true;
            }

            auto stopRecording()  -> void { mRecording = false; }

            auto dump( std::ostream & out) const -> void
            {
                //
                // A streamed recording has already been written, and there is
                // nothing left here to write -- so this would quietly produce an
                // empty file, very possibly on top of the recording that was
                // just streamed into it. Refused rather than allowed to mean
                // something surprising.
                //
                if( mRecorder.isStreaming())
                {
                    throw std::logic_error(
                        "SessionBank::dump: this recording was streamed as it was taken and is already written");
                }

                writeRecording( out, mRecorder.samples());
            }

            [[nodiscard]]
            auto samples() const -> const std::vector<RecordedSample> &
            {
                return mRecorder.samples();
            }

            // How many observations this run has recorded -- see
            // core::RecordingSession::recordedCount.
            [[nodiscard]]
            auto recordedCount() const -> std::uint64_t
            {
                return mRecorder.recordedCount();
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
            LiveSession         mLive;
            PlaceholderSession  mPlaceholder;
            ScriptedSession     mScripted;
            SwitchableSession   mSwitchable{ mLive };
            RecordingSession    mRecorder{ mSwitchable };
            bool                mRecording{ false };
    };
} // namespace core
