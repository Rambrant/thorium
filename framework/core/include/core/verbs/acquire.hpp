#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/session/bench.hpp"
#include "core/driver/describe.hpp"
#include "core/journal/journal.hpp"
#include "core/session/session.hpp"

namespace core
{
    //
    // Arm/Await: the triggered-acquisition verbs -- tell an instrument to
    // capture the next event it sees, and later find out whether it did.
    //
    // The verb pair exists because of a shape no other verb in this framework
    // can express: an observation whose *stimulus comes from somewhere else*.
    // Every other reading here is self-contained -- Measure connects, reads and
    // disconnects within the call; Read asks a port what it has. A triggered
    // capture is split in half by construction, because the thing being
    // captured happens between the halves and is caused by the script:
    //
    //     Arm(   Osc1.single().timeout( 2_s));      // scope armed and waiting
    //     Remove( DcP1.dc());                       // <-- the event to capture
    //     const auto captured = Await( Osc1.single());
    //
    // Deliberately generic, in core, and named for the operation rather than
    // for an oscilloscope. "Arm a capture, let the event happen, find out
    // whether it landed" is what a transient recorder does, what a digitizer
    // does, what a counter with an armed gate does, and what a DMM in triggered
    // burst mode does. hal::keysight_dsox1202g::DSOX1202G is simply the driver that answers to
    // armDriver/awaitDriver here, exactly as hal::racal1260::Racal1260 was the first to answer
    // to writeDriver/readDriver (see core/verbs/transfer.hpp's own comment on the
    // same choice).
    //
    // ---------------------------------------------------------------------
    // Why Arm blocks, and why that is the whole point
    // ---------------------------------------------------------------------
    //
    // Arm does not return when the instrument has been *told* to capture. It
    // returns when the instrument is armed and ready to trigger. Those are
    // different moments, and the gap between them is where a single-shot test
    // silently fails: the event fires while the scope is still setting itself
    // up, nothing triggers, and the run reports a missing transient that was
    // in fact present.
    //
    // Keysight's own Infiniium programmer's reference gives this sequence for
    // precisely this case, under "Single Shot Device Under Test":
    //
    //     :STOP; *OPC?          // settle
    //     :ADER?                // clear the acquisition-done event
    //     :SINGLE               // arm
    //     while (AER? == 0) wait(100ms)
    //     // oscilloscope is armed and ready, enable DUT here
    //
    // The comment on that last line is the post-condition Arm is written to
    // provide. So a script's ordering rule is simply: Arm, then cause the
    // event. That is the same shape as core/verbs/source.hpp's Connect-before-Apply
    // rule, and it is stated here for the same reason -- it is a property of
    // the hardware, not of this code, and nothing below enforces it.
    //
    // The manual is equally explicit about what NOT to do, and it is what the
    // legacy ATE script this repo replaces did: arm, wait a fixed 20 ms, and
    // hope. "Too often, fixed delays are used to accomplish this wait, but
    // fixed delays often use excessive time or the time may not be long
    // enough."
    //
    // ---------------------------------------------------------------------
    // Why Await is here and Arm is not, as far as sessions go
    // ---------------------------------------------------------------------
    //
    // Await goes through the core::SessionBank, exactly as Measure and Read
    // do, and Arm does not -- the same split those two already draw (see
    // core/verbs/transfer.hpp). An Arm is stimulus: it goes straight to the
    // instrument, and a replayed run has nothing to reproduce about it. An
    // Await is an observation, and one the rest of the script depends on: a
    // replay that took its voltages from a recording but asked absent hardware
    // whether there had been anything to capture would answer "no" and fail
    // every check beneath it, for a reason found nowhere in the file.
    //
    // That is also why AwaitEngine is constructed from a bank rather than
    // owning one -- one run, one ordered recording, covering all three seams.
    // See core::SessionBank's own comment, and hal/verbs/measure.hpp for where this
    // rig's three reading verbs are tied to the same bank.
    //
    class ArmEngine
    {
        public:
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) const -> void
            {
                //
                // Conditional on a bench being attached, like every other
                // stimulus verb -- see core/session/bench.hpp. Await is not: it
                // observes, and answers from the recording.
                //
                if( bench().isAttached())
                {
                    armDriver( builder.config());
                }

                //
                // Logged after the driver call, for the reason every engine in
                // core/verbs/source.hpp gives: if arming failed, nothing is armed and
                // nothing should claim it is. With settings -- an arm carries
                // its timeout and whatever else the builder named, and a log
                // that omitted them could not explain a capture that timed out.
                //
                detail::postSourceEvent( Verb::Arm, builder.config(), true);
            }
    };

    //
    // Waits for the armed capture to complete, and answers whether it did.
    //
    // Returns bool rather than throwing on timeout, and that is a deliberate
    // reading of what a timeout means. "The transient never arrived" is a
    // finding about the DUT, not an error in the bench -- it is exactly the
    // thing a test may have been written to check -- so it comes back as a
    // value a criterion can be pointed at:
    //
    //     Verify( "Transient captured", EQ( true), captured);
    //
    // [[nodiscard]], for the same reason: an Await whose answer is dropped on
    // the floor is a script that waited and then measured whatever happened to
    // be in the acquisition buffer from before -- which is the failure this
    // verb exists to make impossible to write by accident.
    //
    class AwaitEngine
    {
        public:
            explicit AwaitEngine( SessionBank & sessions) : mSessions( sessions) {}

            template<typename BuilderT>
            [[nodiscard]]
            auto operator()( const BuilderT & builder) -> bool
            {
                const auto described = describeConfig( builder.config());

                //
                // Sessions key by name, and a capture has no DUT point to key
                // by -- so it keys the way core::ReadEngine and Measure's
                // point-free readback overload already do:
                // "<instrument>.<what>", here "Osc1.Acquisition".
                //
                // Which is what a script unit test injects against:
                //
                //     Await.inject( "Osc1.Acquisition", true);
                //
                const auto key = described.Instrument + ".Acquisition";

                auto livePoll = [&]() -> bool
                {
                    return awaitDriver( builder.config());
                };

                const auto completed = mSessions.active().fetchFlag( key, described.Instrument, livePoll);

                journal().post( JournalRecord{
                    .Method     = Verb::Await,
                    .Subject    = key,
                    .Detail     = completed ? "acquisition complete" : "timed out before the acquisition completed",
                    .Instrument = described.Instrument,
                    .Value      = completed ? "complete" : "timeout"
                });

                return completed;
            }

            //
            // Feeds a canned answer, bypassing hal entirely -- the flag-side
            // twin of Measure.inject and Read.inject, and what makes a script
            // built around a single-shot capture testable with no bench.
            //
            auto inject( const std::string_view name, const bool value) -> void
            {
                mSessions.injectFlag( name, value);
            }

            //
            // A *sequence* of answers, one per Await, in order -- for a script
            // that captures more than once, or a test that wants the second
            // capture to be the one that times out. Running past the end
            // throws (see core::ScriptedSession::fetchFlag).
            //
            auto inject( const std::string_view name, std::initializer_list<bool> values) -> void
            {
                mSessions.injectFlag( name, flagSourceOf( std::vector<bool>( values)));
            }

            auto inject( const std::string_view name, FlagSource source) -> void
            {
                mSessions.injectFlag( name, std::move( source));
            }

            //
            // Deliberately absent: useLive, load, startRecording, stopRecording
            // and dump -- see core::ReadEngine's own comment on why the bank is
            // reached through Measure for all five and offered only once.
            //
        private:
            SessionBank & mSessions;
    };
} // namespace core
